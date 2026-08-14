/**
 * @file    xfer_session.c
 * @brief   State machine for one in-flight WiFi file transfer.
 *
 * Timing / timeouts (see PROT-001 §5.5):
 *
 *   WAIT_CONFIRM   30s   user must accept the offer
 *   WAIT_STA       60s   phone STA must associate with our SoftAP
 *   RECV          120s   idle-in-transfer watchdog (no data seen)
 *   OVERALL       180s   whole thing from OFFER to DONE
 *
 * The state machine is driven by:
 *   - BLE handlers (offer, user_decision)
 *   - port_softap callback (on_sta_joined)
 *   - port_tcp callback (on_data, on_close)
 *   - ebadge_task tick (~100ms) -- for the four timeouts above
 *
 * All callbacks from other threads MUST be marshalled via
 * ebadge_task_post_call() before invoking anything here.
 *
 * Progress throttling (PROT-001 §4.8):  emit 0x14 PROGRESS at least every
 * 200ms OR at each new 5% boundary, whichever comes first.
 */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "xfer_session.h"
#include "ebxf_frame.h"

#include "../ebadge_cmd.h"
#include "../ebadge_l2.h"
#include "../ebadge_errcode.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#include "../port/ebadge_port_softap.h"
#include "../port/ebadge_port_tcp.h"
#include "../port/ebadge_port_storage.h"

/*----------------------------------------------------------------------------*
 *  Config
 *----------------------------------------------------------------------------*/
#define XS_WAIT_CONFIRM_MS   30000u
#define XS_WAIT_STA_MS       60000u
#define XS_RECV_IDLE_MS     120000u
#define XS_OVERALL_MS       180000u

#define XS_TCP_PORT           EB_AP_DEFAULT_PORT
#define XS_PROGRESS_MS         200
#define XS_PROGRESS_STEP_PCT     5

/*----------------------------------------------------------------------------*
 *  Session state  (l2_task-owned; no locking)
 *----------------------------------------------------------------------------*/
typedef struct
{
    xfer_session_state_t state;

    /* offer parameters ------------------------------------------------- */
    char     name[EBXF_NAME_LEN + 1];
    uint8_t  file_type;
    uint32_t size;
    uint32_t crc32_expected;
    uint16_t replace_id;

    /* runtime ---------------------------------------------------------- */
    uint32_t bytes_recv;
    uint32_t crc32_running;
    bool     ebxf_seen;
    int      wp_handle;

    /* timers (deadlines in ebadge_task_now_ms units) ------------------ */
    uint32_t deadline_stage;      /* per-state deadline                  */
    uint32_t deadline_overall;    /* whole-session hard cap              */
    uint32_t last_data_ms;        /* rolls the RECV-idle deadline        */
    uint32_t last_progress_ms;
    uint8_t  last_progress_pct;
} xfer_ctx_t;

static xfer_ctx_t s_x;

/*----------------------------------------------------------------------------*
 *  Local helpers -- notify emission
 *
 *  Every builder below follows the TLV order given in the spec section named
 *  in its comment.  Type numbers are per-command (spec §2.4), so the macro
 *  prefix must match the command being built -- EB_TLV_DEC_* only inside
 *  0x11, EB_TLV_AP_* only inside 0x13, and so on.
 *----------------------------------------------------------------------------*/

/** 0x11 XFER_DECISION (spec §4.6).  @p reason is optional: pass 0 to omit. */
static void emit_decision(uint8_t decision, uint8_t reason)
{
    uint8_t  params[16];
    uint16_t off = 0;
    ebadge_tlv_put_u8(params, sizeof(params), &off,
                      EB_TLV_DEC_DECISION, decision);
    if (reason != 0)
    {
        ebadge_tlv_put_u8(params, sizeof(params), &off,
                          EB_TLV_DEC_REASON, reason);
    }
    (void)ebadge_l2_notify_send(EB_CMD_XFER_DECISION, params, off);
}

/** 0x13 AP_INFO (spec §4.7) -- all seven TLVs are required. */
static void emit_ap_info(const ebadge_softap_info_t *info)
{
    /* Worst case: (3+32)+(3+63)+4+7+5+4+4 = 125 bytes.  Sized with slack so
     * a max-length SSID + PSK cannot silently drop the trailing TLVs.     */
    uint8_t  params[160];
    uint16_t off  = 0;
    uint16_t pwlen = (uint16_t)strlen(info->password);

    ebadge_tlv_put(params, sizeof(params), &off, EB_TLV_AP_SSID,
                   (const uint8_t *)info->ssid,
                   (uint16_t)strlen(info->ssid));
    ebadge_tlv_put(params, sizeof(params), &off, EB_TLV_AP_PASSWORD,
                   (const uint8_t *)info->password, pwlen);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_CHANNEL,
                      info->channel);
    /* IPv4 goes out in NETWORK order (192.168.4.1 -> C0 A8 04 01), so it
     * cannot use put_u32, which would emit it little-endian.               */
    uint8_t ipb[4] =
    {
        (uint8_t)((info->ip >> 24) & 0xFFu),
        (uint8_t)((info->ip >> 16) & 0xFFu),
        (uint8_t)((info->ip >>  8) & 0xFFu),
        (uint8_t)((info->ip) & 0xFFu),
    };
    ebadge_tlv_put(params, sizeof(params), &off, EB_TLV_AP_IPV4, ipb, 4);
    ebadge_tlv_put_u16(params, sizeof(params), &off, EB_TLV_AP_PORT,
                       XS_TCP_PORT);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_PROTO,
                      EB_AP_PROTO_RAW_TCP);
    ebadge_tlv_put_u8(params, sizeof(params), &off, EB_TLV_AP_SECURITY,
                      pwlen ? EB_AP_SEC_WPA2_PSK : EB_AP_SEC_OPEN);
    (void)ebadge_l2_notify_send(EB_CMD_AP_INFO, params, off);
}

/** 0x14 XFER_PROGRESS (spec §4.8) -- absolute byte counters, not a percent. */
static void emit_progress(uint32_t recv, uint32_t total)
{
    uint8_t  params[16];
    uint16_t off = 0;
    ebadge_tlv_put_u32(params, sizeof(params), &off, EB_TLV_PROG_RECV,  recv);
    ebadge_tlv_put_u32(params, sizeof(params), &off, EB_TLV_PROG_TOTAL, total);
    (void)ebadge_l2_notify_send(EB_CMD_XFER_PROGRESS, params, off);
}

/** 0x15 XFER_DONE (spec §4.9) -- all three TLVs required. */
static void emit_done(uint16_t file_id)
{
    uint8_t  params[64];
    uint16_t off = 0;
    ebadge_tlv_put_u16(params, sizeof(params), &off,
                       EB_TLV_DONE_FILE_ID, file_id);
    ebadge_tlv_put_u32(params, sizeof(params), &off,
                       EB_TLV_DONE_SIZE, s_x.size);
    ebadge_tlv_put(params, sizeof(params), &off,
                   EB_TLV_DONE_NAME,
                   (const uint8_t *)s_x.name,
                   (uint16_t)strlen(s_x.name));
    (void)ebadge_l2_notify_send(EB_CMD_XFER_DONE, params, off);
}

/** 0x16 XFER_FAIL (spec §4.10).  @p detail is optional: pass NULL to omit. */
static void emit_fail(uint8_t reason, const char *detail)
{
    uint8_t  params[32];
    uint16_t off = 0;
    ebadge_tlv_put_u8(params, sizeof(params), &off,
                      EB_TLV_FAIL_REASON, reason);
    if (detail && detail[0])
    {
        size_t n = strlen(detail);
        if (n > EB_MAX_MSG_STR) { n = EB_MAX_MSG_STR; }
        ebadge_tlv_put(params, sizeof(params), &off,
                       EB_TLV_FAIL_DETAIL, (const uint8_t *)detail,
                       (uint16_t)n);
    }
    (void)ebadge_l2_notify_send(EB_CMD_XFER_FAIL, params, off);
}

/*----------------------------------------------------------------------------*
 *  State transitions
 *----------------------------------------------------------------------------*/
static void reset_ctx(void)
{
    memset(&s_x, 0, sizeof(s_x));
    s_x.state     = XFER_SESSION_IDLE;
    s_x.wp_handle = 0;
}

static void tear_down_data_plane(void)
{
    if (ebadge_port_softap_running())
    {
        (void)ebadge_port_softap_stop();
    }
    (void)ebadge_port_tcp_close();
    if (s_x.wp_handle > 0)
    {
        (void)ebadge_port_storage_wp_abort(s_x.wp_handle);
        s_x.wp_handle = 0;
    }
}

static void fail_and_reset(uint8_t reason, const char *detail)
{
    /* Nack over TCP if the link is still up.  EBXR carries the same §2.6
     * reason byte as the BLE 0x16 FAIL, so the App sees one code on both
     * planes -- and status 0x00 is FAILURE per spec §5.3.                 */
    uint8_t ack[EBXR_LEN];
    ebxr_pack(ack, EBXR_STATUS_FAILED, reason);
    (void)ebadge_port_tcp_send(ack, EBXR_LEN);

    tear_down_data_plane();
    emit_fail(reason, detail);
    reset_ctx();
}

/*----------------------------------------------------------------------------*
 *  Bring-up on ACCEPT: raise SoftAP + arm TCP listen
 *----------------------------------------------------------------------------*/
static void on_softap_joined_from_driver(void)
{
    /* Called on softap driver thread -- marshal to l2_task. */
    (void)ebadge_task_post_call((ebadge_post_fn_t)xfer_session_on_sta_joined, NULL);
}

static void on_tcp_data_from_driver(const uint8_t *data, uint16_t len)
{
    /* on driver thread; the impl of port_tcp is expected to allocate a
     * heap copy + post_call.  Here we only receive the "already on
     * l2_task" version through xfer_session_on_tcp_data(); this
     * function itself is the raw callback registered with port_tcp and
     * must therefore heap-copy + post.                                   */
    if (len == 0) { return; }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) { return; }
    memcpy(copy, data, len);
    /* Small trampoline: we cannot pack (ptr, len) into a single void*
     * unless we allocate a wrapper.  Use a 2-field struct.               */
    struct chunk { uint8_t *p; uint16_t n; } *c =
        (struct chunk *)malloc(sizeof(*c));
    if (!c) { free(copy); return; }
    c->p = copy; c->n = len;
    /* Trampoline fn defined below to unpack and free.                    */
    extern void xfer_data_trampoline(void *arg);
    (void)ebadge_task_post_call(xfer_data_trampoline, c);
}

void xfer_data_trampoline(void *arg)
{
    struct chunk { uint8_t *p; uint16_t n; } *c = arg;
    if (!c) { return; }
    xfer_session_on_tcp_data(c->p, c->n);
    free(c->p);
    free(c);
}

static void on_tcp_close_from_driver(ebadge_tcp_close_reason_t r)
{
    /* Pack the reason into arg (small enough); avoid heap.               */
    (void)ebadge_task_post_call((ebadge_post_fn_t)xfer_session_on_tcp_close,
                                (void *)(uintptr_t)r);
}

/*----------------------------------------------------------------------------*
 *  Tick handler -- runs on l2_task every EBADGE_TICK_MS
 *----------------------------------------------------------------------------*/
static void on_tick(uint32_t now_ms)
{
    if (s_x.state == XFER_SESSION_IDLE)
    {
        return;
    }

    /* Overall session hard deadline. */
    if ((int32_t)(now_ms - s_x.deadline_overall) >= 0)
    {
        EBADGE_WARN("xfer: overall deadline hit");
        fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "overall timeout");
        return;
    }

    switch (s_x.state)
    {
    case XFER_SESSION_WAIT_CONFIRM:
        if ((int32_t)(now_ms - s_x.deadline_stage) >= 0)
        {
            EBADGE_WARN("xfer: user did not confirm in 30s");
            /* Spec §4.6: decision=TIMEOUT, reason=USER_TIMEOUT. */
            emit_decision(EB_DECISION_TIMEOUT, EB_XFER_ERR_USER_TIMEOUT);
            reset_ctx();
        }
        break;

    case XFER_SESSION_WAIT_STA:
        if ((int32_t)(now_ms - s_x.deadline_stage) >= 0)
        {
            EBADGE_WARN("xfer: STA did not join in 60s");
            fail_and_reset(EB_XFER_ERR_STA_TIMEOUT, "sta join timeout");
        }
        break;

    case XFER_SESSION_RECV:
        /* Idle watchdog: if no TCP data for XS_RECV_IDLE_MS, bail. */
        if ((int32_t)(now_ms - (s_x.last_data_ms + XS_RECV_IDLE_MS)) >= 0)
        {
            EBADGE_WARN("xfer: RECV idle, bail");
            fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "recv idle");
        }
        break;

    case XFER_SESSION_COMPLETING:
    case XFER_SESSION_IDLE:
    default:
        break;
    }
}

/*----------------------------------------------------------------------------*
 *  CRC32 (IEEE 802.3, poly 0xEDB88320) -- streaming
 *----------------------------------------------------------------------------*/
static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, uint16_t len)
{
    crc = ~crc;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
        {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void xfer_session_init(void)
{
    reset_ctx();
    ebadge_task_set_tick(on_tick);
}

xfer_session_state_t xfer_session_state(void)
{
    return s_x.state;
}

uint32_t xfer_session_bytes_received(void) { return s_x.bytes_recv; }
uint32_t xfer_session_total_size(void)     { return s_x.size; }

/*----------------------------------------------------------------------------*/
void xfer_session_offer(const char *name, uint8_t file_type,
                        uint32_t size, uint32_t crc32, uint16_t replace_id)
{
    if (s_x.state != XFER_SESSION_IDLE)
    {
        EBADGE_WARN1("offer while busy (state=%d) -> XFER_FAIL BUSY", (int)s_x.state);
        emit_fail(EB_XFER_ERR_BUSY, "busy");
        return;
    }

    /* Type check (spec §2.7): 0x00 UNKNOWN is illegal in an offer, and
     * anything above the enum's last member is a newer App than us.       */
    if (file_type < EB_FILE_TYPE_JPEG || file_type > EB_FILE_TYPE_MAX)
    {
        EBADGE_WARN1("offer: unsupported file_type=0x%02x -> XFER_FAIL FMT_UNSUPPORTED",
                     file_type);
        emit_fail(EB_XFER_ERR_FMT_UNSUPPORTED, "bad file_type");
        return;
    }

    /* A zero-length offer has nothing to receive; the EBXF header would also
     * fail its size cross-check later.  Reject up front as a verify error.  */
    if (size == 0)
    {
        EBADGE_WARN("offer: size=0 -> XFER_FAIL VERIFY");
        emit_fail(EB_XFER_ERR_VERIFY, "size=0");
        return;
    }

    /* Space check (spec §2.9): free must cover the payload plus the fixed
     * 4096-byte filesystem metadata margin.  Both sides are BYTES -- the
     * pre-alignment code compared a KB field against a byte count.        */
    ebadge_storage_stat_t st;
    if (ebadge_port_storage_stat(&st) == 0)
    {
        uint64_t need = (uint64_t)size + EB_FS_MARGIN;
        if (st.free_bytes < need)
        {
            EBADGE_WARN2("offer: no space, need=%u free=%u -> XFER_FAIL STORAGE_FULL",
                         (unsigned)need, (unsigned)st.free_bytes);
            emit_fail(EB_XFER_ERR_STORAGE_FULL, "storage full");
            return;
        }
    }

    /* Stash the offer.  Advance to WAIT_CONFIRM. */
    reset_ctx();
    if (name)
    {
        size_t n = strlen(name);
        if (n > EB_MAX_FILE_NAME) { n = EB_MAX_FILE_NAME; }
        memcpy(s_x.name, name, n);
        s_x.name[n] = '\0';
    }
    s_x.file_type       = file_type;
    s_x.size            = size;
    s_x.crc32_expected  = crc32;
    s_x.replace_id      = replace_id;
    s_x.state           = XFER_SESSION_WAIT_CONFIRM;

    uint32_t now = ebadge_task_now_ms();
    s_x.deadline_stage   = now + XS_WAIT_CONFIRM_MS;
    s_x.deadline_overall = now + XS_OVERALL_MS;

    EBADGE_LOG2("xfer: offer accepted for approval, size=%u type=%d",
                (unsigned)size, (int)file_type);
    EBADGE_LOG("xfer: WAIT_CONFIRM (30s)  -- awaiting user_decision()");
    /* App / UI is expected to eventually call xfer_session_user_decision().  */
}

void xfer_session_user_decision(bool accept)
{
    if (s_x.state != XFER_SESSION_WAIT_CONFIRM)
    {
        EBADGE_WARN1("user_decision in wrong state=%d", (int)s_x.state);
        return;
    }

    if (!accept)
    {
        EBADGE_LOG("xfer: user REJECT -> DECISION 0x00 reason=USER_REJECT");
        emit_decision(EB_DECISION_REJECT, EB_XFER_ERR_USER_REJECT);
        reset_ctx();
        return;
    }

    /* Notify decision first, then bring up the data plane. */
    EBADGE_LOG("xfer: user ACCEPT -> DECISION 0x01");
    emit_decision(EB_DECISION_ACCEPT, 0);

    ebadge_softap_info_t info =
    {
        .ssid     = "eBadge-XFR",
        .password = "ebadge-xfer",
        .channel  = 6,
        .ip       = ((uint32_t)EB_AP_DEFAULT_IPV4_A << 24)
        | ((uint32_t)EB_AP_DEFAULT_IPV4_B << 16)
        | ((uint32_t)EB_AP_DEFAULT_IPV4_C <<  8)
        | (uint32_t)EB_AP_DEFAULT_IPV4_D,
    };
    if (ebadge_port_softap_start(&info, on_softap_joined_from_driver) != 0)
    {
        EBADGE_ERR("xfer: softap_start FAIL -> XFER_FAIL AP_START");
        fail_and_reset(EB_XFER_ERR_AP_START, "softap start");
        return;
    }
    ebadge_tcp_listen_t lc =
    {
        .port     = XS_TCP_PORT,
        .on_data  = on_tcp_data_from_driver,
        .on_close = on_tcp_close_from_driver,
    };
    if (ebadge_port_tcp_listen(&lc) != 0)
    {
        /* §2.6 has no separate listener code -- a failed listen leaves the
         * App with no reachable endpoint, which is exactly AP_START.       */
        EBADGE_ERR("xfer: tcp_listen FAIL -> XFER_FAIL AP_START");
        fail_and_reset(EB_XFER_ERR_AP_START, "tcp listen");
        return;
    }

    /* Emit AP_INFO so the App can associate. */
    EBADGE_LOG2("xfer: -> AP_INFO ssid=\"%s\" tcp_port=%d",
                info.ssid, XS_TCP_PORT);
    emit_ap_info(&info);

    s_x.state         = XFER_SESSION_WAIT_STA;
    s_x.deadline_stage = ebadge_task_now_ms() + XS_WAIT_STA_MS;
    EBADGE_LOG("xfer: WAIT_STA (60s)");
}

void xfer_session_on_sta_joined(void)
{
    if (s_x.state != XFER_SESSION_WAIT_STA)
    {
        EBADGE_WARN1("sta_joined in wrong state=%d", (int)s_x.state);
        return;
    }
    /* Open write session; ready to receive. */
    s_x.wp_handle = ebadge_port_storage_wp_begin(s_x.name, s_x.size,
                                                 s_x.file_type);
    if (s_x.wp_handle < 0)
    {
        fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_begin");
        return;
    }
    s_x.state        = XFER_SESSION_RECV;
    s_x.last_data_ms = ebadge_task_now_ms();
    EBADGE_LOG("xfer: RECV started");
}

void xfer_session_on_tcp_data(const uint8_t *data, uint16_t len)
{
    if (s_x.state != XFER_SESSION_RECV || data == NULL || len == 0)
    {
        return;
    }
    uint32_t now = ebadge_task_now_ms();
    s_x.last_data_ms = now;

    uint16_t consumed = 0;

    /* First-time-only: consume the 40B EBXF header before body bytes. */
    if (!s_x.ebxf_seen)
    {
        if (len < EBXF_HDR_LEN)
        {
            /* Header would need reassembly.  Simple path: drop and fail;
             * the delivery contract asks for >=4KB batches, so a fragmented
             * header is anomalous.  A real impl could stash + resume.     */
            EBADGE_ERR("xfer: EBXF header split across delivery, unsupported");
            fail_and_reset(EB_XFER_ERR_VERIFY, "ebxf split");
            return;
        }
        ebxf_hdr_t hdr;
        if (ebxf_hdr_parse(data, &hdr) != 0)
        {
            EBADGE_ERR("xfer: bad EBXF magic/version");
            fail_and_reset(EB_XFER_ERR_VERIFY, "ebxf magic");
            return;
        }
        /* Spec §5.2: the header must restate the offer's size / type / crc.
         * Any disagreement means the two planes describe different files,
         * which is a verification failure (§2.6 0x09).                    */
        if (hdr.file_size != s_x.size || hdr.file_type != s_x.file_type ||
            hdr.crc32     != s_x.crc32_expected)
        {
            EBADGE_WARN2("xfer: EBXF mismatch size=%u type=%d",
                         hdr.file_size, hdr.file_type);
            fail_and_reset(EB_XFER_ERR_VERIFY, "ebxf mismatch");
            return;
        }
        s_x.ebxf_seen = true;
        consumed      = EBXF_HDR_LEN;
    }

    /* Body bytes: append + accumulate CRC + progress notify. */
    if (consumed < len)
    {
        const uint8_t *body = data + consumed;
        uint16_t       n    = (uint16_t)(len - consumed);

        int rc = ebadge_port_storage_wp_write(s_x.wp_handle, body, n);
        if (rc < 0)
        {
            fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_write");
            return;
        }
        s_x.crc32_running = crc32_update(s_x.crc32_running, body, n);
        s_x.bytes_recv   += n;

        /* Progress throttling: fire on 5%-boundary crossings OR 200ms lapse.
         * The percentage is an emission trigger only -- the payload carries
         * absolute recv/total byte counts per spec §4.8.                  */
        uint8_t pct = (s_x.size ? (uint8_t)((uint64_t)s_x.bytes_recv * 100u
                                            / s_x.size) : 0);
        bool step_hit = pct >= (uint8_t)(s_x.last_progress_pct
                                         + XS_PROGRESS_STEP_PCT);
        bool time_hit = (int32_t)(now - (s_x.last_progress_ms
                                         + XS_PROGRESS_MS)) >= 0;
        if (step_hit || time_hit)
        {
            emit_progress(s_x.bytes_recv, s_x.size);
            s_x.last_progress_pct = pct;
            s_x.last_progress_ms  = now;
        }
    }

    /* Done? */
    if (s_x.bytes_recv >= s_x.size)
    {
        s_x.state = XFER_SESSION_COMPLETING;

        /* CRC verify. */
        if (s_x.crc32_running != s_x.crc32_expected)
        {
            EBADGE_ERR2("xfer: CRC mismatch got=0x%08x exp=0x%08x",
                        s_x.crc32_running, s_x.crc32_expected);
            fail_and_reset(EB_XFER_ERR_VERIFY, "crc32");
            return;
        }

        uint16_t file_id = 0;
        int rc = ebadge_port_storage_wp_commit(s_x.wp_handle, &file_id);
        s_x.wp_handle = 0;
        if (rc < 0)
        {
            fail_and_reset(EB_XFER_ERR_STORAGE_FULL, "wp_commit");
            return;
        }
        /* Send EBXR ok + tear down data plane.  status=0x01 is SUCCESS and
         * reason is 0 on success (spec §5.3).                             */
        uint8_t ack[EBXR_LEN];
        ebxr_pack(ack, EBXR_STATUS_OK, 0);
        (void)ebadge_port_tcp_send(ack, EBXR_LEN);
        (void)ebadge_port_tcp_close();
        (void)ebadge_port_softap_stop();

        /* 0x15 DONE is authoritative for the App, not the EBXR above.     */
        emit_done(file_id);
        reset_ctx();
    }
}

void xfer_session_on_tcp_close(int reason)
{
    if (s_x.state == XFER_SESSION_IDLE ||
        s_x.state == XFER_SESSION_COMPLETING)
    {
        /* Expected close after commit -- nothing to do. */
        return;
    }
    EBADGE_WARN1("xfer: tcp closed mid-session reason=%d", reason);
    fail_and_reset(EB_XFER_ERR_IO_TIMEOUT, "tcp closed");
}

void xfer_session_abort(void)
{
    if (s_x.state == XFER_SESSION_IDLE)
    {
        return;
    }
    EBADGE_LOG("xfer: abort");
    tear_down_data_plane();
    /* No notify -- the BLE link is probably gone (we are called from
     * the disconnect hook).  A subsequent OFFER will start fresh.        */
    reset_ctx();
}

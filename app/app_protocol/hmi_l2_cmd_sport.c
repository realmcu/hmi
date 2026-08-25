/*
 * hmi_l2_cmd_sport.c
 *
 * SPORT command (0x05) version 1 -- history sync. See BLE_PROTOCOL_SPEC
 * section 3.6 for the authoritative wire format.
 *
 * Implemented: activity data (Data type 0x01) backed by the pedo TSDB via
 * app_health_history_read().
 *
 * NOT implemented: sleep data (Data type 0x02, reply key 0x03). The storage
 * layer doesn't exist yet -- no sleep TSDB, no sleep_state_record_t, no read
 * API. Per spec, a request for an unsupported Data type must NOT create a
 * session, so SPORT_REQ with 0x02 is dropped without any reply.
 *
 * Session model (spec 3.6 "版本与通用约束")
 * ----------------------------------------
 * At most one sync session per BLE link at a time. The first SPORT_REQ opens
 * a session at the oldest record; each subsequent SPORT_REQ must repeat the
 * same Data type and advances the cursor. The phone must wait for SPORT_MORE
 * before asking for the next page, so exactly one page is emitted per request
 * and this handler never loops on proto_send().
 *
 * Threading: runs on l2_task. proto_send() blocks awaiting an ACK, which is
 * fine here because a request yields at most three sends (START + page + MORE
 * or END) before returning. The session is dropped on EVT_BLE_DISCONNECTED.
 */

#include "hmi_l2_cmd_sport.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "app_health.h"
#include "app_time.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Session state. Only touched from l2_task (command handler) and from the
 * disconnect hook, which is bounced onto l2_task as well.
 *
 * There is no read cursor here: app_health owns the watermark and never
 * hands out the same record twice, so a session is just "is a sync in
 * flight, and for which Data type". s_sync_sent is for logging.
 *
 * Consequence worth knowing: records taken from app_health cannot be taken
 * again. See sport_send_activity_page() for how a failed send is handled. */
static bool     s_sync_active;
static uint8_t  s_sync_dtype;
static int      s_sync_sent;

/*============================================================================*
 *                              Wire helpers
 *============================================================================*/

/* Emit one SPORT key with the given value. Header is always "05 10". */
static bool sport_send(uint8_t key, const uint8_t *val, uint16_t val_len)
{
    /* Largest value is a full activity page: 1 + 8*18 = 145 bytes. */
    uint8_t  buf[2u + 3u + 1u + HMI_L2_SPORT_RECS_MAX * HMI_L2_SPORT_REC_SIZE];
    uint16_t pos = 0;

    buf[pos++] = HMI_L2_CMD_SPORT;
    buf[pos++] = HMI_L2_SPORT_VER;

    buf[pos++] = key;
    buf[pos++] = (uint8_t)(val_len >> 8);
    buf[pos++] = (uint8_t)(val_len & 0xFFu);

    for (uint16_t i = 0; i < val_len; i++)
    {
        buf[pos++] = val[i];
    }

    return proto_send(buf, pos);
}

/* SYNC_START / SYNC_END / SPORT_MORE all carry a single Data type byte. */
static bool sport_send_dtype(uint8_t key, uint8_t dtype)
{
    return sport_send(key, &dtype, 1u);
}

/* Serialise one record big-endian, field by field.
 *
 * health_pedo_record_t is a packed little-endian struct; the wire format is
 * big-endian. Copying the struct bytes wholesale would silently byte-swap
 * every multi-byte field -- the spec calls this out explicitly
 * ("禁止直接复制 MCU 内存中的结构体字节").
 *
 * The flag bits line up 1:1 by value: HEALTH_RECORD_FLAG_HAS_HR == bit0 ==
 * Flags.HAS_HR, HEALTH_RECORD_FLAG_PARTIAL_BUCKET == bit1 ==
 * Flags.PARTIAL_BUCKET, so flags passes through unchanged.
 *
 * Returns the number of bytes written (always HMI_L2_SPORT_REC_SIZE). */
static uint16_t sport_encode_record(uint8_t *out, const health_pedo_record_t *r)
{
    uint16_t p = 0;

    out[p++] = (uint8_t)(r->ts >> 24);
    out[p++] = (uint8_t)(r->ts >> 16);
    out[p++] = (uint8_t)(r->ts >> 8);
    out[p++] = (uint8_t)(r->ts);

    out[p++] = (uint8_t)(r->steps >> 8);
    out[p++] = (uint8_t)(r->steps);

    out[p++] = (uint8_t)(r->distance_m >> 8);
    out[p++] = (uint8_t)(r->distance_m);

    out[p++] = (uint8_t)(r->calories_dkcal >> 8);
    out[p++] = (uint8_t)(r->calories_dkcal);

    /* Spec: Heart rate must be 0 unless Flags.HAS_HR is set. */
    out[p++] = (r->flags & HEALTH_RECORD_FLAG_HAS_HR) ? r->hr_avg : 0u;

    out[p++] = r->bucket_min;
    out[p++] = r->mode;
    out[p++] = r->flags;

    /* Reserved: forward the stored value as-is, big-endian. */
    out[p++] = (uint8_t)(r->reserved >> 24);
    out[p++] = (uint8_t)(r->reserved >> 16);
    out[p++] = (uint8_t)(r->reserved >> 8);
    out[p++] = (uint8_t)(r->reserved);

    return p;
}

/*============================================================================*
 *                              Activity paging
 *============================================================================*/

static void sport_session_reset(void)
{
    s_sync_active = false;
    s_sync_dtype  = 0;
    s_sync_sent   = 0;
}

/* Read up to one frame's worth of records and emit them, then tell the phone
 * whether to ask again.
 *
 * Reads are one-way: app_health never hands the same record out twice, so
 * anything taken here that fails to reach the phone is lost. Records are taken
 * one at a time and encoded as they arrive, so the loop never runs ahead of
 * what we intend to send.
 *
 * There is also no way to peek past the page boundary -- asking whether a
 * ninth record exists would consume it. So a full page always advertises MORE,
 * and the next request finding the store empty closes with END. When the
 * record count is an exact multiple of the page size that costs one extra
 * empty round trip. */
static void sport_send_activity_page(void)
{
    uint8_t  val[1u + HMI_L2_SPORT_RECS_MAX * HMI_L2_SPORT_REC_SIZE];
    uint16_t pos = 1u;                 /* val[0] is the record count */
    uint8_t  n   = 0u;

    while (n < HMI_L2_SPORT_RECS_MAX)
    {
        health_pedo_record_t rec;
        int rc = app_health_history_read(&rec);
        if (rc < 0)
        {
            /* Storage unavailable. Drop the session; the phone retries on its
             * next connection. Records already encoded into val are discarded
             * unsent -- accepted, because the alternative is sending a partial
             * page and then having to explain the failure anyway. */
            PROTO_LOG("L2 SPORT history_read failed rc=%d, aborting session", rc);
            sport_session_reset();
            return;
        }
        if (rc == 0)
        {
            break;                     /* no unread records left */
        }

        pos += sport_encode_record(&val[pos], &rec);
        n++;

        /* Log which interval just went out, as wall-clock time. Seconds are
         * omitted because buckets are quarter-hour aligned. */
        struct tm lt;
        app_time_to_calendar(rec.ts, &lt);
        PROTO_LOG("L2 SPORT rec %u: %04u-%02u-%02u %02u:%02u steps=%u",
                  (unsigned)n,
                  (unsigned)(lt.tm_year + 1900), (unsigned)(lt.tm_mon + 1),
                  (unsigned)lt.tm_mday, (unsigned)lt.tm_hour,
                  (unsigned)lt.tm_min, (unsigned)rec.steps);
    }

    /* Spec: an empty store must NOT produce a Record count=0 page -- go
     * straight from START to END. */
    if (n > 0u)
    {
        val[0] = n;

        if (!sport_send(HMI_L2_SPORT_DATA_RSP, val, pos))
        {
            PROTO_LOG("L2 SPORT page send failed, aborting session "
                      "(%u record(s) consumed but not delivered)", (unsigned)n);
            sport_session_reset();
            return;
        }

        s_sync_sent += n;
        PROTO_LOG("L2 SPORT page sent count=%u sent=%d", (unsigned)n, s_sync_sent);
    }

    if (n == HMI_L2_SPORT_RECS_MAX)
    {
        (void)sport_send_dtype(HMI_L2_SPORT_MORE, HMI_L2_SPORT_DT_ACTIVITY);
    }
    else
    {
        (void)sport_send_dtype(HMI_L2_SYNC_END, HMI_L2_SPORT_DT_ACTIVITY);
        PROTO_LOG("L2 SPORT sync complete, %d record(s) sent", s_sync_sent);
        sport_session_reset();
    }
}

/*============================================================================*
 *                              Request handling
 *============================================================================*/

static void sport_on_req(const hmi_l2_kv_t *kv)
{
    /* Spec: v-length must be exactly 1; a mismatched value is discarded.
     *
     * A val_len of 0 specifically means the peer is speaking the DEPRECATED
     * version 0 dialect, where SPORT_REQ carried no Data type at all. Do not
     * paper over it: version 0 also packs sport records as 8-byte bitfields
     * (vs. our 18-byte fields), dates as packed year/month/day + an 11-bit
     * intra-day offset (vs. Unix timestamps), and requires MORE/START/END to
     * carry an EMPTY value -- a v1 device reply would fail to parse on a v0
     * peer anyway. Defaulting the Data type here would buy nothing. */
    if (kv->val == NULL || kv->val_len != 1u)
    {
        if (kv->val_len == 0u)
        {
            PROTO_LOG("L2 SPORT REQ val_len=0: peer speaks deprecated version 0, "
                      "no session (v0 wire format is incompatible end-to-end)");
        }
        else
        {
            PROTO_LOG("L2 SPORT REQ bad val_len=%u, discard", (unsigned)kv->val_len);
        }
        return;
    }

    uint8_t dtype = kv->val[0];

    if (dtype == HMI_L2_SPORT_DT_SLEEP)
    {
        /* Sleep has no storage layer yet. Do not open a session -- the phone
         * times out rather than being told a lie about an empty sleep DB. */
        PROTO_LOG("L2 SPORT REQ sleep not implemented, ignoring");
        return;
    }
    if (dtype != HMI_L2_SPORT_DT_ACTIVITY)
    {
        /* Reserved Data type: must not create a session. */
        PROTO_LOG("L2 SPORT REQ reserved dtype=0x%02x, ignoring", dtype);
        return;
    }

    if (!s_sync_active)
    {
        /* First request: announce the start before any data page.
         *
         * Nothing to open -- app_health's watermark already knows where the
         * unread history begins, and it persists across connections, so a
         * reconnect resumes rather than restarting. */
        s_sync_active = true;
        s_sync_dtype  = dtype;
        s_sync_sent   = 0;

        if (!sport_send_dtype(HMI_L2_SYNC_START, dtype))
        {
            PROTO_LOG("L2 SPORT SYNC_START send failed");
            sport_session_reset();
            return;
        }
        PROTO_LOG("L2 SPORT sync started dtype=0x%02x", dtype);
    }
    else if (s_sync_dtype != dtype)
    {
        /* Spec: a session must not switch Data type mid-flight. */
        PROTO_LOG("L2 SPORT REQ dtype=0x%02x mismatches session 0x%02x, ignoring",
                  dtype, s_sync_dtype);
        return;
    }

    sport_send_activity_page();
}

static void on_cmd_sport(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SPORT   key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);

        if (kvs[i].key == HMI_L2_SPORT_REQ)
        {
            sport_on_req(&kvs[i]);
        }
        /* Every other key in this command is device->phone or reserved;
         * unknown keys are silently ignored and leave the session intact. */
    }
}

/*============================================================================*
 *                              Lifecycle
 *============================================================================*/

/* Runs on app_task. Only clears session state (no flash handles to release, no
 * read cursor to unwind -- app_health's watermark is already persisted), so
 * unlike the xfer handler this needs no bounce onto l2_task: a torn read of the
 * bool/u8/int trio can at worst drop a page the dead link couldn't carry. */
static void on_ble_disconnected(app_event_id_t id, const void *payload,
                                size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;

    if (s_sync_active)
    {
        PROTO_LOG("L2 SPORT link lost, dropping sync session");
    }
    sport_session_reset();
}

void hmi_l2_sport_register(void)
{
    hmi_l2_register(HMI_L2_CMD_SPORT, on_cmd_sport);
    (void)app_event_subscribe(EVT_BLE_DISCONNECTED, on_ble_disconnected, NULL);
}

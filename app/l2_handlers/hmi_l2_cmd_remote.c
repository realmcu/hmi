#include "hmi_l2_cmd_remote.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

extern void gui_log(const char *format, ...);   /* SoC-side trace channel */

/* -------- Authoritative state mirror (only mutated by parse_state_report) -- */
static uint16_t s_zoom_x100     = 100;   /* 1.0x until app pushes a snapshot */
static bool     s_recording     = false;
static uint8_t  s_facing        = 0;     /* 0=back / 1=front */
static bool     s_has_last_shot = false;
static uint16_t s_last_shot_id  = 0;

/* -------- Callbacks (all nullable) ---------------------------------------- */
static hmi_l2_remote_state_cb_t       s_state_cb = NULL;
static hmi_l2_remote_shot_cb_t        s_shot_cb  = NULL;
static hmi_l2_remote_ctrl_result_cb_t s_ctrl_cb  = NULL;

/*============================================================================*
 * STATE_REPORT TLV parser (spec 5.2).  Unknown tags are silently skipped for
 * forward-compat -- do not reject the packet.
 *============================================================================*/
static void parse_state_report(const uint8_t *p, uint16_t len)
{
    uint16_t off     = 0;
    bool     changed = false;

    /* Diag trace: prove the packet reached the handler, and print raw bytes so
     * we can eyeball tag / vlen / endianness on the wire. Remove once P0 is
     * verified end-to-end. */
    gui_log("remote/rx: STATE_REPORT len=%u\n", (unsigned)len);
    {
        char hex[3 * 32 + 4];
        uint16_t n = (len > 32u) ? 32u : len;
        uint16_t i, o = 0;
        for (i = 0; i < n; i++)
        {
            static const char H[] = "0123456789ABCDEF";
            hex[o++] = H[(p[i] >> 4) & 0xF];
            hex[o++] = H[p[i] & 0xF];
            hex[o++] = ' ';
        }
        if (len > 32u) { hex[o++] = '.'; hex[o++] = '.'; hex[o++] = '.'; }
        hex[o] = '\0';
        gui_log("remote/rx: bytes=%s\n", hex);
    }

    while (off + 2u <= len)
    {
        uint8_t tag  = p[off++];
        uint8_t vlen = p[off++];
        if ((uint32_t)off + vlen > len)
        {
            PROTO_LOG("L2 RC: STATE truncated at tag 0x%02x", tag);
            break;
        }

        switch (tag)
        {
        case HMI_L2_RC_TAG_RECORDING:
            if (vlen >= 1u)
            {
                bool v = (p[off] != 0);
                if (v != s_recording) { s_recording = v; changed = true; }
            }
            break;

        case HMI_L2_RC_TAG_FACING:
            if (vlen >= 1u)
            {
                uint8_t v = p[off];
                if (v != s_facing) { s_facing = v; changed = true; }
            }
            break;

        case HMI_L2_RC_TAG_ZOOM_X100:
            if (vlen >= 2u)
            {
                uint16_t v = ((uint16_t)p[off] << 8) | p[off + 1u];
                gui_log("remote/rx: tag=ZOOM v=%u (cur=%u)\n",
                        (unsigned)v, (unsigned)s_zoom_x100);
                if (v != s_zoom_x100) { s_zoom_x100 = v; changed = true; }
            }
            break;

        case HMI_L2_RC_TAG_HAS_LAST_SHOT:
            if (vlen >= 1u)
            {
                bool v = (p[off] != 0);
                if (v != s_has_last_shot) { s_has_last_shot = v; changed = true; }
            }
            break;

        case HMI_L2_RC_TAG_LAST_SHOT_ID:
            if (vlen >= 2u)
            {
                uint16_t v = ((uint16_t)p[off] << 8) | p[off + 1u];
                if (v != s_last_shot_id) { s_last_shot_id = v; changed = true; }
            }
            break;

        default:
            /* Unknown tag -- skip payload; forward-compat per spec 5.2 */
            break;
        }
        off += vlen;
    }

    if (changed && s_state_cb) { s_state_cb(); }
    if (!changed) { gui_log("remote/rx: STATE parsed, no field changed\n"); }
}

static void parse_ctrl_result(const uint8_t *p, uint16_t len)
{
    if (len < 3u)
    {
        PROTO_LOG("L2 RC: CTRL_RESULT bad len=%u", len);
        return;
    }
    uint8_t key  = p[0];
    uint8_t code = p[1];
    /* p[2] = detail, reserved (0) */
    PROTO_LOG("L2 RC: CTRL_RESULT key=0x%02x code=0x%02x", key, code);
    if (s_ctrl_cb) { s_ctrl_cb(key, code); }
}

static void parse_last_shot_ready(const uint8_t *p, uint16_t len)
{
    if (len < 2u)
    {
        PROTO_LOG("L2 RC: LAST_SHOT_READY bad len=%u", len);
        return;
    }
    uint16_t shot_id = ((uint16_t)p[0] << 8) | p[1];
    /* p[2] = reserved (0) if present */
    PROTO_LOG("L2 RC: LAST_SHOT_READY shot_id=%u", shot_id);
    if (s_shot_cb) { s_shot_cb(shot_id); }
}

/*============================================================================*
 * RX dispatch
 *============================================================================*/
static void on_cmd_remote(const hmi_l2_kv_t *kvs, uint8_t n)
{
    gui_log("remote/rx: on_cmd_remote n_kv=%u\n", (unsigned)n);
    for (uint8_t i = 0; i < n; i++)
    {
        uint8_t key = kvs[i].key;
        gui_log("remote/rx:  kv[%u] key=0x%02x len=%u\n",
                (unsigned)i, (unsigned)key, (unsigned)kvs[i].val_len);

        /* Spec section 8 loopback: the app never sends control (0x01-0x0F) to
         * the device.  If we see one, it's an echo or a mis-framed packet --
         * drop and log, do not act on it. */
        if (key >= 0x01u && key <= 0x0fu)
        {
            PROTO_LOG("L2 RC: unexpected control-key 0x%02x, drop", key);
            continue;
        }

        switch (key)
        {
        case HMI_L2_RC_STATE_REPORT:
            parse_state_report(kvs[i].val, kvs[i].val_len);
            break;
        case HMI_L2_RC_CTRL_RESULT:
            parse_ctrl_result(kvs[i].val, kvs[i].val_len);
            break;
        case HMI_L2_RC_LAST_SHOT_READY:
            parse_last_shot_ready(kvs[i].val, kvs[i].val_len);
            break;
        default:
            /* preview segment (0x20-0x2F) and reserved: ignored in P0 */
            PROTO_LOG("L2 RC: unhandled key 0x%02x", key);
            break;
        }
    }
}

/*============================================================================*
 * TX: Dev -> App requests
 *============================================================================*/
int hmi_l2_remote_send_capture(void)
{
    /* L2_HDR(2) + KV_PREFIX(3) + payload(0) */
    uint8_t buf[5] =
    {
        HMI_L2_CMD_REMOTE, 0x00,
        HMI_L2_RC_CAPTURE, 0x00, 0x00,
    };
    return proto_send(buf, sizeof(buf));
}

int hmi_l2_remote_send_set_zoom(uint16_t zoom_x100)
{
    /* Spec section 11 P0: SET_ZOOM does NOT clamp on the device -- we forward
     * the raw value and let the app reject with CTRL_RESULT OUT_OF_RANGE when
     * it is out of [100, 400]. */
    uint8_t buf[9] =
    {
        HMI_L2_CMD_REMOTE, 0x00,
        HMI_L2_RC_SET_ZOOM, 0x00, 0x04,
        (uint8_t)(zoom_x100 >> 8), (uint8_t)(zoom_x100 & 0xff),
        0x00, 0x00,     /* reserved u16 */
    };
    return proto_send(buf, sizeof(buf));
}

/*============================================================================*
 * Getters -- UI reads authoritative state from here
 *============================================================================*/
uint16_t hmi_l2_remote_state_zoom_x100(void)     { return s_zoom_x100;     }
bool     hmi_l2_remote_state_recording(void)     { return s_recording;     }
uint8_t  hmi_l2_remote_state_facing(void)        { return s_facing;        }
bool     hmi_l2_remote_state_has_last_shot(void) { return s_has_last_shot; }
uint16_t hmi_l2_remote_state_last_shot_id(void)  { return s_last_shot_id;  }

void hmi_l2_remote_set_state_cb(hmi_l2_remote_state_cb_t cb)             { s_state_cb = cb; }
void hmi_l2_remote_set_shot_cb(hmi_l2_remote_shot_cb_t cb)               { s_shot_cb  = cb; }
void hmi_l2_remote_set_ctrl_result_cb(hmi_l2_remote_ctrl_result_cb_t cb) { s_ctrl_cb  = cb; }

void hmi_l2_remote_register(void)
{
    hmi_l2_register(HMI_L2_CMD_REMOTE, on_cmd_remote);
}

#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

/*============================================================================*
 *                              L2 Packet Layout
 *
 *  L2 Header (2 bytes, Big-Endian):
 *    Byte 0: Command ID
 *    Byte 1: [7:4] Version  [3:0] Reserve
 *
 *  L2 Payload (0~502 bytes), repeated Key-Value triplets:
 *    1 byte   Key
 *    2 bytes  Key Header (BE): [15:9] Reserve  [8:0] v-length
 *    N bytes  Key Value  (N = v-length, may be 0)
 *============================================================================*/

#define L2_HDR_LEN      2u
#define L2_KV_PREFIX    3u   /* 1 (key) + 2 (key header) */
#define L2_MAX_KVS      16u

typedef struct
{
    uint8_t        key;
    uint16_t       val_len;
    const uint8_t *val;
} hmi_l2_kv_t;

/*============================================================================*
 *                              Parser
 *============================================================================*/

static uint8_t parse_kvs(const uint8_t *payload, uint16_t len,
                         hmi_l2_kv_t *kvs, uint8_t max_kvs)
{
    uint16_t pos = 0;
    uint8_t  n   = 0;

    while (pos + L2_KV_PREFIX <= len && n < max_kvs)
    {
        kvs[n].key     = payload[pos];
        uint16_t khdr  = ((uint16_t)payload[pos + 1] << 8) | payload[pos + 2];
        kvs[n].val_len = khdr;              /* full 16-bit v-length */
        pos += L2_KV_PREFIX;

        if (pos + kvs[n].val_len > len)
        {
            PROTO_LOG("hmi_l2: key=0x%02x val overflows payload, discard", kvs[n].key);
            break;
        }

        kvs[n].val = payload + pos;
        pos += kvs[n].val_len;
        n++;
    }

    return n;
}

/*============================================================================*
 *                              Command handlers (stubs)
 *
 *  Fill in application logic below.  Each handler receives the parsed
 *  key-value array; use the HMI_L2_* key defines from hmi_l2.h.
 *============================================================================*/

static void on_cmd_ota(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 OTA     key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_settings(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SETTINGS key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_bind(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 BIND    key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_notify(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 NOTIFY  key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_sport(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SPORT   key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_factory(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 FACTORY key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_control(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 CONTROL key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

static void on_cmd_log(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 LOG     key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              File transfer (0x0b)
 *============================================================================*/

static bool     s_xfer_active   = false;
static uint8_t  s_xfer_type     = 0;
static uint32_t s_xfer_total    = 0;
static uint16_t s_xfer_chunk    = 0;
static uint16_t s_xfer_next_seq = 0;

static void xfer_send(uint8_t key, const uint8_t *val, uint16_t val_len)
{
    uint8_t buf[2 + 3 + 8];   /* L2_HDR + KV_PREFIX + max value (3 bytes) */
    uint16_t pos = 0;

    buf[pos++] = HMI_L2_CMD_FILE_XFER;
    buf[pos++] = 0x00u;
    buf[pos++] = key;
    buf[pos++] = (uint8_t)((val_len >> 8) & 0x01u);
    buf[pos++] = (uint8_t)(val_len & 0xFFu);
    for (uint16_t i = 0; i < val_len; i++)
    {
        buf[pos++] = val[i];
    }
    proto_send(buf, pos);
}

static void xfer_reset(void)
{
    s_xfer_active   = false;
    s_xfer_type     = 0;
    s_xfer_total    = 0;
    s_xfer_chunk    = 0;
    s_xfer_next_seq = 0;
}

static void on_cmd_xfer(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        uint8_t        key = kvs[i].key;
        const uint8_t *val = kvs[i].val;
        uint16_t       vl  = kvs[i].val_len;

        switch (key)
        {
        case HMI_L2_XFER_BEGIN_REQ:
            {
                if (vl < 8)
                {
                    PROTO_LOG("L2 XFER BEGIN_REQ too short (%d)", vl);
                    break;
                }
                if (s_xfer_active)
                {
                    uint8_t rsp[3] = { HMI_L2_XFER_BEGIN_BUSY, 0x00u, 0x00u };
                    xfer_send(HMI_L2_XFER_BEGIN_RSP, rsp, sizeof(rsp));
                    break;
                }
                s_xfer_type  = val[0];
                s_xfer_total = ((uint32_t)val[1] << 24) | ((uint32_t)val[2] << 16)
                               | ((uint32_t)val[3] <<  8) | (uint32_t)val[4];
                s_xfer_chunk = ((uint16_t)val[5] << 8) | val[6];
                if (s_xfer_chunk == 0 || s_xfer_chunk > HMI_L2_XFER_CHUNK_MAX)
                {
                    s_xfer_chunk = HMI_L2_XFER_CHUNK_MAX;
                }
                s_xfer_next_seq = 0;
                s_xfer_active   = true;

                uint8_t fname_len = val[7];
                PROTO_LOG("L2 XFER BEGIN type=%d total=%lu chunk=%d fname_len=%d",
                          s_xfer_type, (unsigned long)s_xfer_total,
                          s_xfer_chunk, fname_len);

                uint8_t rsp[3];
                rsp[0] = HMI_L2_XFER_BEGIN_OK;
                rsp[1] = (uint8_t)(s_xfer_chunk >> 8);
                rsp[2] = (uint8_t)(s_xfer_chunk & 0xFFu);
                xfer_send(HMI_L2_XFER_BEGIN_RSP, rsp, sizeof(rsp));
                break;
            }

        case HMI_L2_XFER_DATA:
            {
                PROTO_LOG("L2 XFER DATA active=%d vl=%d val[0..3]=%02x %02x %02x %02x next_seq=%d",
                          s_xfer_active, vl,
                          vl > 0 ? val[0] : 0xFF, vl > 1 ? val[1] : 0xFF,
                          vl > 2 ? val[2] : 0xFF, vl > 3 ? val[3] : 0xFF,
                          s_xfer_next_seq);
                if (!s_xfer_active || vl < 2)
                {
                    PROTO_LOG("L2 XFER DATA discard: active=%d vl=%d", s_xfer_active, vl);
                    break;
                }
                uint16_t seq = ((uint16_t)val[0] << 8) | val[1];
                if (seq != s_xfer_next_seq)
                {
                    PROTO_LOG("L2 XFER DATA seq=%d expected=%d, aborting", seq, s_xfer_next_seq);
                    uint8_t reason = HMI_L2_XFER_ABORT_ERROR;
                    xfer_send(HMI_L2_XFER_ABORT, &reason, 1);
                    xfer_reset();
                    break;
                }
                PROTO_LOG("L2 XFER DATA seq=%d data_len=%d", seq, vl - 2);
                /* TODO: write (val + 2, vl - 2) to storage */
                s_xfer_next_seq++;
                break;
            }

        case HMI_L2_XFER_END_REQ:
            {
                if (!s_xfer_active || vl < 4)
                {
                    break;
                }
                uint32_t crc32 = ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16)
                                 | ((uint32_t)val[2] <<  8) | (uint32_t)val[3];
                PROTO_LOG("L2 XFER END crc32=0x%08lx total_chunks=%d",
                          (unsigned long)crc32, s_xfer_next_seq);
                /* TODO: verify crc32 against received data */
                xfer_reset();
                uint8_t rsp[2] = { HMI_L2_XFER_END_OK, HMI_L2_XFER_ERR_NONE };
                xfer_send(HMI_L2_XFER_END_RSP, rsp, sizeof(rsp));
                break;
            }

        case HMI_L2_XFER_ABORT:
            {
                uint8_t reason = (vl > 0) ? val[0] : 0;
                PROTO_LOG("L2 XFER ABORT reason=%d", reason);
                xfer_reset();
                break;
            }

        default:
            PROTO_LOG("L2 XFER unknown key=0x%02x", key);
            break;
        }
    }
}

/*============================================================================*
 *                              Public API
 *============================================================================*/

void hmi_l2_handle(const uint8_t *data, uint16_t len)
{
    if (!data || len < L2_HDR_LEN)
    {
        PROTO_LOG("hmi_l2_handle: too short (%d)", len);
        return;
    }

    uint8_t cmd_id = data[0];
    /* uint8_t version = (data[1] >> 4) & 0x0Fu; */

    hmi_l2_kv_t kvs[L2_MAX_KVS];
    uint8_t     n = parse_kvs(data + L2_HDR_LEN, len - L2_HDR_LEN, kvs, L2_MAX_KVS);

    PROTO_LOG("hmi_l2_handle: cmd=0x%02x kv_count=%d", cmd_id, n);

    switch (cmd_id)
    {
    case HMI_L2_CMD_OTA:       on_cmd_ota(kvs, n);      break;
    case HMI_L2_CMD_SETTINGS:  on_cmd_settings(kvs, n); break;
    case HMI_L2_CMD_BIND:      on_cmd_bind(kvs, n);     break;
    case HMI_L2_CMD_NOTIFY:    on_cmd_notify(kvs, n);   break;
    case HMI_L2_CMD_SPORT:     on_cmd_sport(kvs, n);    break;
    case HMI_L2_CMD_FACTORY:   on_cmd_factory(kvs, n);  break;
    case HMI_L2_CMD_CONTROL:   on_cmd_control(kvs, n);  break;
    case HMI_L2_CMD_LOG:       on_cmd_log(kvs, n);      break;
    case HMI_L2_CMD_FILE_XFER: on_cmd_xfer(kvs, n);     break;
    default:
        PROTO_LOG("hmi_l2_handle: unknown cmd=0x%02x", cmd_id);
        break;
    }
}

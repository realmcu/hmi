#include "hmi_l2.h"
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
        kvs[n].val_len = khdr & 0x01FFu;   /* bit[8:0] = v-length */
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
    default:
        PROTO_LOG("hmi_l2_handle: unknown cmd=0x%02x", cmd_id);
        break;
    }
}

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
#define L2_MAX_CMD      0x1Fu

/*============================================================================*
 *                              Handler table
 *============================================================================*/

static hmi_l2_cmd_handler_t s_handlers[L2_MAX_CMD];

void hmi_l2_register(uint8_t cmd_id, hmi_l2_cmd_handler_t handler)
{
    if (cmd_id < L2_MAX_CMD)
    {
        s_handlers[cmd_id] = handler;
    }
}

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

    hmi_l2_kv_t kvs[L2_MAX_KVS];
    uint8_t     n = parse_kvs(data + L2_HDR_LEN, len - L2_HDR_LEN, kvs, L2_MAX_KVS);

    PROTO_LOG("hmi_l2_handle: cmd=0x%02x kv_count=%d", cmd_id, n);

    if (cmd_id < L2_MAX_CMD && s_handlers[cmd_id])
    {
        s_handlers[cmd_id](kvs, n);
    }
    else
    {
        PROTO_LOG("hmi_l2_handle: no handler for cmd=0x%02x", cmd_id);
    }
}

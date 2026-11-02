#include "hmi_l2.h"
#include "proto_log.h"

#include <stdbool.h>

/*============================================================================*
 *                              L2 Packet Layout
 *
 *  L2 Header (2 bytes, Big-Endian):
 *    Byte 0: Command ID
 *    Byte 1: [7:4] Version  [3:0] Reserve
 *
 *  L2 Payload, repeated Key-Value triplets:
 *    1 byte   Key
 *    2 bytes  Key Header (BE): [15:0] v-length (max 65535)
 *    N bytes  Key Value  (N = v-length, may be 0)
 *
 *  The Key Header used to be [15:9] Reserve + [8:0] v-length (max 511).
 *  It was widened to a full 16-bit v-length so upper layers can carry large
 *  values. A v-length of 0 still occupies the full 2 bytes. Frame capacity is
 *  bounded by PROTO_MAX_PAYLOAD_LEN, not by this field.
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

static bool parse_kvs(const uint8_t *payload, uint16_t len,
                      hmi_l2_kv_t *kvs, uint8_t max_kvs, uint8_t *out_n)
{
    uint16_t pos = 0;
    uint8_t  n   = 0;

    while (pos < len)
    {
        if (n >= max_kvs)
        {
            return false;
        }

        if ((uint16_t)(len - pos) < L2_KV_PREFIX)
        {
            return false;
        }

        uint8_t  key  = payload[pos];
        uint16_t khdr = ((uint16_t)payload[pos + 1] << 8) | payload[pos + 2];
        pos += L2_KV_PREFIX;

        if (khdr > (uint16_t)(len - pos))
        {
            PROTO_LOG("hmi_l2: key=0x%02x value overflows payload", key);
            return false;
        }

        kvs[n].key     = key;
        kvs[n].val_len = khdr;
        kvs[n].val = payload + pos;
        pos += khdr;
        n++;
    }

    *out_n = n;
    return true;
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

    uint8_t cmd_id  = data[0];
    uint8_t version = (uint8_t)(data[1] >> 4);

    hmi_l2_kv_t kvs[L2_MAX_KVS];
    uint8_t     n = 0;

    if (!parse_kvs(data + L2_HDR_LEN, len - L2_HDR_LEN,
                   kvs, L2_MAX_KVS, &n))
    {
        PROTO_LOG("hmi_l2_handle: malformed packet cmd=0x%02x", cmd_id);
        return;
    }

    PROTO_LOG("hmi_l2_handle: cmd=0x%02x ver=%u kv_count=%u",
              cmd_id, (unsigned)version, (unsigned)n);

    if (cmd_id < L2_MAX_CMD && s_handlers[cmd_id])
    {
        s_handlers[cmd_id](version, kvs, n);
    }
    else
    {
        PROTO_LOG("hmi_l2_handle: no handler for cmd=0x%02x", cmd_id);
    }
}

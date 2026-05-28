#include "hmi_l2_handlers.h"
#include "protocol/hmi_l2.h"
#include "protocol/hmi_proto.h"
#include "protocol/proto_log.h"
#include "bluetooth/hmi_ble/hmi_ble_conn.h"

/*============================================================================*
 *                              OTA (0x01)
 *============================================================================*/

static void on_cmd_ota(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 OTA     key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Settings (0x02)
 *============================================================================*/

static void on_cmd_settings(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SETTINGS key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Bind / Login (0x03)
 *============================================================================*/

static void on_cmd_bind(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 BIND    key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Notify (0x04)
 *============================================================================*/

static void on_cmd_notify(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 NOTIFY  key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Sport / Health (0x05)
 *============================================================================*/

static void on_cmd_sport(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SPORT   key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Factory test (0x06)
 *============================================================================*/

static void on_cmd_factory(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 FACTORY key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Device control (0x07)
 *============================================================================*/

static void on_cmd_control(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 CONTROL key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

/*============================================================================*
 *                              Log transfer (0x0a)
 *============================================================================*/

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
    uint8_t buf[2 + 3 + 8];
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
 *                              BLE connection parameters (0x0c)
 *============================================================================*/

static void on_cmd_conn_param(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        if (kvs[i].key != HMI_L2_CONN_PARAM_REQ)
        {
            continue;
        }

        hmi_ble_conn_info_t info;
        if (!hmi_ble_get_conn_info(&info))
        {
            PROTO_LOG("L2 CONN_PARAM: not connected");
            break;
        }

        uint8_t buf[2 + 3 + 7];  /* L2_HDR + KV_PREFIX + value */
        uint16_t pos = 0;

        buf[pos++] = HMI_L2_CMD_CONN_PARAM;
        buf[pos++] = 0x00;

        buf[pos++] = HMI_L2_CONN_PARAM_RSP;
        buf[pos++] = 0x00;
        buf[pos++] = 0x07;

        buf[pos++] = (uint8_t)(info.conn_interval >> 8);
        buf[pos++] = (uint8_t)(info.conn_interval & 0xFF);
        buf[pos++] = (uint8_t)(info.conn_latency >> 8);
        buf[pos++] = (uint8_t)(info.conn_latency & 0xFF);
        buf[pos++] = (uint8_t)(info.conn_supervision_timeout >> 8);
        buf[pos++] = (uint8_t)(info.conn_supervision_timeout & 0xFF);
        buf[pos++] = (uint8_t)(info.conn_mtu_size & 0xFF);

        proto_send(buf, pos);

        PROTO_LOG("L2 CONN_PARAM: interval=%d latency=%d timeout=%d mtu=%d",
                  info.conn_interval, info.conn_latency,
                  info.conn_supervision_timeout, info.conn_mtu_size);
    }
}

/*============================================================================*
 *                              Registration
 *============================================================================*/

void hmi_l2_handlers_register(void)
{
    hmi_l2_register(HMI_L2_CMD_OTA,        on_cmd_ota);
    hmi_l2_register(HMI_L2_CMD_SETTINGS,   on_cmd_settings);
    hmi_l2_register(HMI_L2_CMD_BIND,       on_cmd_bind);
    hmi_l2_register(HMI_L2_CMD_NOTIFY,     on_cmd_notify);
    hmi_l2_register(HMI_L2_CMD_SPORT,      on_cmd_sport);
    hmi_l2_register(HMI_L2_CMD_FACTORY,    on_cmd_factory);
    hmi_l2_register(HMI_L2_CMD_CONTROL,    on_cmd_control);
    hmi_l2_register(HMI_L2_CMD_LOG,        on_cmd_log);
    hmi_l2_register(HMI_L2_CMD_FILE_XFER,  on_cmd_xfer);
    hmi_l2_register(HMI_L2_CMD_CONN_PARAM, on_cmd_conn_param);
}

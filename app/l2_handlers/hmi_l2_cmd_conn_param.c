#include "hmi_l2_cmd_conn_param.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"
#include "hmi_ble_conn.h"

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

void hmi_l2_conn_param_register(void)
{
    hmi_l2_register(HMI_L2_CMD_CONN_PARAM, on_cmd_conn_param);
}

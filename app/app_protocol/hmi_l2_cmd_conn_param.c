#include "hmi_l2_cmd_conn_param.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"
#include "app_event.h"
#include "app_event_defs.h"

#include <stdbool.h>
#include <string.h>

/* Latest snapshot cached from EVT_BLE_CONN_PARAM. Valid only when
 * s_have_info == true; cleared on EVT_BLE_DISCONNECTED so a stale RSP
 * isn't sent for a link that has since dropped. */
static app_evt_ble_conn_param_t s_last_info;
static bool                     s_have_info;

static void on_ble_conn_param(app_event_id_t id, const void *payload,
                              size_t len, void *user)
{
    (void)id; (void)user;
    if (payload == NULL || len != sizeof(app_evt_ble_conn_param_t))
    {
        return;
    }
    memcpy(&s_last_info, payload, sizeof s_last_info);
    s_have_info = true;
}

static void on_ble_disconnected(app_event_id_t id, const void *payload,
                                size_t len, void *user)
{
    (void)id; (void)payload; (void)len; (void)user;
    s_have_info = false;
    memset(&s_last_info, 0, sizeof s_last_info);
}

static void on_cmd_conn_param(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        if (kvs[i].key != HMI_L2_CONN_PARAM_REQ)
        {
            continue;
        }

        if (!s_have_info)
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

        buf[pos++] = (uint8_t)(s_last_info.conn_interval >> 8);
        buf[pos++] = (uint8_t)(s_last_info.conn_interval & 0xFF);
        buf[pos++] = (uint8_t)(s_last_info.conn_latency >> 8);
        buf[pos++] = (uint8_t)(s_last_info.conn_latency & 0xFF);
        buf[pos++] = (uint8_t)(s_last_info.conn_supervision_timeout >> 8);
        buf[pos++] = (uint8_t)(s_last_info.conn_supervision_timeout & 0xFF);
        buf[pos++] = (uint8_t)(s_last_info.conn_mtu_size & 0xFF);

        proto_send(buf, pos);

        PROTO_LOG("L2 CONN_PARAM: interval=%d latency=%d timeout=%d mtu=%d",
                  s_last_info.conn_interval, s_last_info.conn_latency,
                  s_last_info.conn_supervision_timeout,
                  s_last_info.conn_mtu_size);
    }
}

void hmi_l2_conn_param_register(void)
{
    hmi_l2_register(HMI_L2_CMD_CONN_PARAM, on_cmd_conn_param);
    (void)app_event_subscribe(EVT_BLE_CONN_PARAM,   on_ble_conn_param,   NULL);
    (void)app_event_subscribe(EVT_BLE_DISCONNECTED, on_ble_disconnected, NULL);
}

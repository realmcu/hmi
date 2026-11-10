#include "hmi_l2_cmd_settings.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"
#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"
#include <gap.h>

static void send_device_info(void)
{
    uint8_t rsp[] =
    {
        HMI_L2_CMD_SETTINGS, 0x00u, HMI_L2_DEVICE_INFO, 0x00u, 0x12u,
        0x02u,             /* read response */
        0x00u, 0x01u,      /* model */
        0x01u, 0x02u, 0x03u, /* firmware 1.2.3 */
        0x00u, 0x64u,      /* build 100 */
        0x01u,             /* protocol version */
        0x01u,             /* hardware version */
        0x01u,             /* platform */
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, /* MAC */
        0x00u,             /* serial length */
    };
    uint8_t bd_addr[6] = {0};
    if (gap_get_param(GAP_PARAM_BD_ADDR, bd_addr) == GAP_CAUSE_SUCCESS)
    {
        for (uint8_t i = 0; i < 6; i++)
        {
            rsp[16u + i] = bd_addr[5u - i];
        }
    }
    (void)proto_send(rsp, sizeof(rsp));
}

static void on_cmd_settings(uint8_t version, const hmi_l2_kv_t *kvs, uint8_t n)
{
    (void)version;
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SETTINGS key=0x%02x val_len=%u",
                  kvs[i].key, (unsigned)kvs[i].val_len);

        if (kvs[i].key == HMI_L2_DEVICE_INFO)
        {
            if (kvs[i].val_len == 1u && kvs[i].val[0] == 0x01u)
            {
                send_device_info();
            }
            continue;
        }
        if (kvs[i].key != HMI_L2_SET_TIME) { continue; }
        if (kvs[i].val_len != 5u || kvs[i].val[0] != 0x00u)
        {
            PROTO_LOG("L2 SETTINGS invalid time value len=%u",
                      (unsigned)kvs[i].val_len);
            continue;
        }

        /* Opcode 0x00 (set) + 4 bytes big-endian wall clock seconds. */
        const uint8_t *v = kvs[i].val + 1;
        uint32_t sec = ((uint32_t)v[0] << 24) |
                       ((uint32_t)v[1] << 16) |
                       ((uint32_t)v[2] <<  8) |
                       (uint32_t)v[3];

        if (app_time_set(sec) != 0)
        {
            PROTO_LOG("L2 SETTINGS failed to set RTC");
            continue;
        }

        app_evt_time_synced_t ev = { .sec = sec };
        if (app_event_publish(EVT_TIME_SYNCED, &ev, sizeof ev) != 0)
        {
            PROTO_LOG("L2 SETTINGS failed to publish EVT_TIME_SYNCED");
        }
    }
}

void hmi_l2_settings_register(void)
{
    hmi_l2_register(HMI_L2_CMD_SETTINGS, on_cmd_settings);
}

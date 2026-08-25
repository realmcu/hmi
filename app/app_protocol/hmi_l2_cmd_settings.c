#include "hmi_l2_cmd_settings.h"
#include "hmi_l2.h"
#include "proto_log.h"
#include "app_time.h"
#include "app_event.h"
#include "app_event_defs.h"

static void on_cmd_settings(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SETTINGS key=0x%02x val_len=%u",
                  kvs[i].key, (unsigned)kvs[i].val_len);

        if (kvs[i].key != HMI_L2_SET_TIME) { continue; }
        if (kvs[i].val_len != 4u)
        {
            PROTO_LOG("L2 SETTINGS invalid time payload len=%u",
                      (unsigned)kvs[i].val_len);
            continue;
        }

        /* 4 bytes big-endian wall clock seconds, 1970 epoch. */
        const uint8_t *v = kvs[i].val;
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

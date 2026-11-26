#include "watch_l2_cmd_record.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "honey_protocol_log.h"
#include "watch_l2.h"

#include <stddef.h>

static void on_cmd_record(uint8_t version, const watch_l2_kv_t *kvs, uint8_t n)
{
    if (version != WATCH_L2_RECORD_VERSION)
    {
        HONEY_PROTOCOL_LOG("L2 RECORD unsupported version=%u", (unsigned)version);
        return;
    }

    for (uint8_t i = 0u; i < n; i++)
    {
        const watch_l2_kv_t *kv = &kvs[i];
        switch (kv->key)
        {
        case WATCH_L2_RECORD_KEY_REQUEST:
            if (kv->val_len == 0u)
            {
                int rc = app_event_publish(EVT_RECORD_START_REQ, NULL, 0u);
                HONEY_PROTOCOL_LOG("L2 RECORD request publish rc=%d", rc);
            }
            break;

        case WATCH_L2_RECORD_KEY_CANCEL:
            if (kv->val_len == 0u)
            {
                app_evt_record_stop_t request = {
                    .reason = WATCH_L2_RECORD_STOP_CANCELLED_BY_PHONE,
                };
                (void)app_event_publish(EVT_RECORD_STOP_REQ, &request, sizeof(request));
            }
            break;

        case WATCH_L2_RECORD_KEY_START:
        case WATCH_L2_RECORD_KEY_DATA:
        case WATCH_L2_RECORD_KEY_STOP:
            /* These keys are device-to-phone only. */
            break;

        default:
            HONEY_PROTOCOL_LOG("L2 RECORD unknown key=0x%02x", kv->key);
            break;
        }
    }
}

void watch_l2_record_register(void)
{
    watch_l2_register(WATCH_L2_CMD_RECORD, on_cmd_record);
}

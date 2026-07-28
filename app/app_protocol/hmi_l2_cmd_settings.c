#include "hmi_l2_cmd_settings.h"
#include "hmi_l2.h"
#include "proto_log.h"
#include "app_event.h"
#include "app_event_defs.h"

static int is_leap_year(uint16_t year)
{
    return ((year % 4u) == 0u && (year % 100u) != 0u) ||
           (year % 400u) == 0u;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] =
    {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u,
    };

    if (month < 1u || month > 12u)
    {
        return 0u;
    }
    if (month == 2u && is_leap_year(year))
    {
        return 29u;
    }
    return days[month - 1u];
}

int hmi_l2_decode_time(const uint8_t *value, uint16_t value_len,
                       app_time_local_t *out)
{
    uint32_t packed;
    app_time_local_t decoded;

    if (value == NULL || value_len != 4u || out == NULL)
    {
        return -1;
    }

    packed = ((uint32_t)value[0] << 24) |
             ((uint32_t)value[1] << 16) |
             ((uint32_t)value[2] << 8) |
             (uint32_t)value[3];

    decoded.year = (uint16_t)(2000u + ((packed >> 26) & 0x3fu));
    decoded.month = (uint8_t)((packed >> 22) & 0x0fu);
    decoded.day = (uint8_t)((packed >> 17) & 0x1fu);
    decoded.hour = (uint8_t)((packed >> 12) & 0x1fu);
    decoded.min = (uint8_t)((packed >> 6) & 0x3fu);
    decoded.sec = (uint8_t)(packed & 0x3fu);
    decoded.weekday = 0u;

    if (decoded.month < 1u || decoded.month > 12u ||
        decoded.day < 1u ||
        decoded.day > days_in_month(decoded.year, decoded.month) ||
        decoded.hour > 23u || decoded.min > 59u || decoded.sec > 59u)
    {
        return -1;
    }

    *out = decoded;
    return 0;
}

static void on_cmd_settings(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        app_time_local_t time;
        app_evt_time_synced_t ev;

        PROTO_LOG("L2 SETTINGS key=0x%02x val_len=%u", kvs[i].key,
                  (unsigned)kvs[i].val_len);
        if (kvs[i].key != HMI_L2_SET_TIME)
        {
            continue;
        }
        if (hmi_l2_decode_time(kvs[i].val, kvs[i].val_len, &time) != 0)
        {
            PROTO_LOG("L2 SETTINGS invalid time payload");
            continue;
        }

        /* Fan out to app_time (and any future listeners) via the event
         * bus rather than reaching into app_time_set_local() directly.
         * The protocol layer's job ends at "decode + validate"; owning
         * the hardware RTC is app_time's concern. */
        ev.year  = time.year;
        ev.month = time.month;
        ev.day   = time.day;
        ev.hour  = time.hour;
        ev.min   = time.min;
        ev.sec   = time.sec;
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

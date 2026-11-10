/*
 * CMD 0x05 health data, L2 version 1.
 *
 * Wire authority: HoneyProtocol docs/protocol/0x05-data.rst.
 *
 * Implemented here:
 *   - 0x00/0x01 history request -> 0x40 snapshot counts, 0x21 activity pages,
 *     0x41 completion, 0x42 phone acknowledgement.
 *   - 0x60 today request -> 0x61 23-byte aggregate.
 *
 * Sleep and other health types are not stored by this firmware yet, so they do
 * not appear in the 0x40 snapshot. The response format is the golden protocol,
 * not the deprecated single-type dialect.
 */

#include "hmi_l2_cmd_sport.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "app_health.h"
#include "app_time.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

static bool s_history_sync_active;
static bool s_history_commit_ready;

static bool sport_send(uint8_t key, const uint8_t *val, uint16_t val_len)
{
    uint8_t buf[2u + 3u + 3u +
                HMI_L2_SPORT_RECS_MAX * HMI_L2_SPORT_REC_SIZE];
    uint16_t pos = 0;

    buf[pos++] = HMI_L2_CMD_SPORT;
    buf[pos++] = HMI_L2_SPORT_VER;
    buf[pos++] = key;
    buf[pos++] = (uint8_t)(val_len >> 8);
    buf[pos++] = (uint8_t)(val_len & 0xFFu);

    for (uint16_t i = 0; i < val_len; i++)
    {
        buf[pos++] = val[i];
    }
    return proto_send(buf, pos);
}

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static uint16_t sport_encode_record(uint8_t *out,
                                    const health_pedo_record_t *record)
{
    uint16_t pos = 0;
    put_u32(&out[pos], record->ts);
    pos += 4u;
    put_u16(&out[pos], record->steps);
    pos += 2u;
    put_u16(&out[pos], record->distance_m);
    pos += 2u;
    put_u16(&out[pos], record->calories_dkcal);
    pos += 2u;
    out[pos++] = (record->flags & HEALTH_RECORD_FLAG_HAS_HR)
                     ? record->hr_avg
                     : 0u;
    out[pos++] = record->bucket_min;
    out[pos++] = record->mode;
    out[pos++] = record->flags;
    put_u32(&out[pos], record->reserved);
    pos += 4u;
    return pos;
}

static bool send_sync_end(uint8_t result, uint32_t actual_count)
{
    uint8_t value[5];
    value[0] = result;
    put_u32(&value[1], actual_count);
    return sport_send(HMI_L2_SPORT_KEY_SYNC_END, value, sizeof(value));
}

static void send_history_sync(void)
{
    if (s_history_sync_active)
    {
        PROTO_LOG("L2 HEALTH sync already active, discard request");
        return;
    }
    s_history_sync_active = true;
    s_history_commit_ready = false;
    app_health_history_reset_uncommitted();

    uint32_t pending = 0u;
    if (app_health_history_pending_count(&pending) != 0)
    {
        PROTO_LOG("L2 HEALTH pending count failed");
        (void)send_sync_end(0x03u, 0u);
        s_history_sync_active = false;
        return;
    }

    uint8_t start[3];
    start[0] = HMI_L2_SPORT_KEY_ACTIVITY_REQ;
    put_u16(&start[1], (uint16_t)((pending > UINT16_MAX) ? UINT16_MAX : pending));
    if (!sport_send(HMI_L2_SPORT_KEY_SYNC_START,
                    pending == 0u ? NULL : start,
                    pending == 0u ? 0u : sizeof(start)))
    {
        PROTO_LOG("L2 HEALTH sync-start send failed");
        s_history_sync_active = false;
        return;
    }

    uint8_t page[3u + HMI_L2_SPORT_RECS_MAX * HMI_L2_SPORT_REC_SIZE];
    uint32_t sent = 0u;
    bool failed = false;

    while (sent < pending)
    {
        uint16_t pos = 3u;
        uint8_t count = 0u;
        while (count < HMI_L2_SPORT_RECS_MAX && sent + count < pending)
        {
            health_pedo_record_t record;
            int rc = app_health_history_read(&record);
            if (rc < 0)
            {
                PROTO_LOG("L2 HEALTH history read failed rc=%d", rc);
                failed = true;
                break;
            }
            if (rc == 0)
            {
                break;
            }
            pos += sport_encode_record(&page[pos], &record);
            count++;
        }

        if (failed) break;
        if (count == 0u) break;

        page[0] = HMI_L2_SPORT_REC_SIZE;
        put_u16(&page[1], count);
        if (!sport_send(HMI_L2_SPORT_KEY_ACTIVITY, page, pos))
        {
            PROTO_LOG("L2 HEALTH activity page send failed");
            failed = true;
            break;
        }
        sent += count;
    }

    bool end_sent = send_sync_end(failed ? 0x03u : 0x00u, sent);
    if (!failed && end_sent)
    {
        s_history_commit_ready = true;
    }
    else
    {
        app_health_history_reset_uncommitted();
    }
    PROTO_LOG("L2 HEALTH sync complete sent=%u pending=%u failed=%d",
              (unsigned)sent, (unsigned)pending, (int)failed);
    s_history_sync_active = false;
}

static void send_today_data(void)
{
    health_daily_rollup_t today;
    app_health_get_today(&today);

    uint8_t value[23];
    put_u32(&value[0], app_time_now());
    put_u32(&value[4], today.steps);
    put_u32(&value[8], today.distance_m);
    put_u32(&value[12], today.calories_dkcal);
    put_u16(&value[16], 0u);
    value[18] = 0xFFu;
    put_u32(&value[19], UINT32_MAX);

    if (!sport_send(HMI_L2_SPORT_KEY_TODAY_DATA, value, sizeof(value)))
    {
        PROTO_LOG("L2 HEALTH today-data send failed");
    }
}

static void on_cmd_health(uint8_t version, const hmi_l2_kv_t *kvs, uint8_t n)
{
    if (version != 0x01u)
    {
        PROTO_LOG("L2 HEALTH unsupported version=%u", (unsigned)version);
        return;
    }

    for (uint8_t i = 0; i < n; i++)
    {
        const hmi_l2_kv_t *kv = &kvs[i];
        PROTO_LOG("L2 HEALTH key=0x%02x val_len=%u",
                  kv->key, (unsigned)kv->val_len);

        switch (kv->key)
        {
        case HMI_L2_SPORT_KEY_REQUEST_ALL:
        case HMI_L2_SPORT_KEY_ACTIVITY_REQ:
            if (kv->val_len == 0u)
            {
                send_history_sync();
            }
            break;
        case HMI_L2_SPORT_KEY_SYNC_ACK:
            if (s_history_commit_ready)
            {
                int rc = app_health_history_commit();
                PROTO_LOG("L2 HEALTH phone acknowledged history sync rc=%d", rc);
                s_history_commit_ready = false;
            }
            break;
        case HMI_L2_SPORT_KEY_TODAY_REQ:
            if (kv->val_len == 0u) send_today_data();
            break;
        default:
            break;
        }
    }
}

static void on_ble_disconnected(app_event_id_t id, const void *payload,
                                size_t len, void *user)
{
    (void)id;
    (void)payload;
    (void)len;
    (void)user;
    s_history_sync_active = false;
    s_history_commit_ready = false;
    app_health_history_reset_uncommitted();
}

void hmi_l2_sport_register(void)
{
    hmi_l2_register(HMI_L2_CMD_SPORT, on_cmd_health);
    (void)app_event_subscribe(EVT_BLE_DISCONNECTED, on_ble_disconnected, NULL);
}

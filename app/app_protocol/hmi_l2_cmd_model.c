#include "hmi_l2_cmd_model.h"

#include "app_model.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

#include <string.h>

static void send_model_snapshot(void)
{
    app_model_snapshot_t model;
    /* Schema v2 value:
     * schema(1), flags(1), battery(1), version_len(1), wall_clock(4),
     * steps(4), distance_m(4), calories_dkcal(4), version(N).
     * All multi-byte integers are encoded big-endian. */
    uint8_t frame[2u + 3u + 20u + APP_MODEL_FIRMWARE_VERSION_MAX];
    uint16_t pos = 0u;
    size_t version_len;
    uint8_t flags = 0u;

    app_model_get_snapshot(&model);
    version_len = strnlen(model.firmware_version, APP_MODEL_FIRMWARE_VERSION_MAX);
    if (model.battery_valid) { flags |= HMI_L2_MODEL_FLAG_BATTERY_VALID; }
    if (model.charging) { flags |= HMI_L2_MODEL_FLAG_CHARGING; }
    if (model.time_valid) { flags |= HMI_L2_MODEL_FLAG_TIME_VALID; }

    frame[pos++] = HMI_L2_CMD_MODEL;
    frame[pos++] = 0x00u;
    frame[pos++] = HMI_L2_MODEL_SNAPSHOT;
    frame[pos++] = 0x00u;
    frame[pos++] = (uint8_t)(20u + version_len);
    frame[pos++] = HMI_L2_MODEL_SCHEMA_V2;
    frame[pos++] = flags;
    frame[pos++] = model.battery_percent;
    frame[pos++] = (uint8_t)version_len;
    frame[pos++] = (uint8_t)(model.wall_clock_sec >> 24);
    frame[pos++] = (uint8_t)(model.wall_clock_sec >> 16);
    frame[pos++] = (uint8_t)(model.wall_clock_sec >> 8);
    frame[pos++] = (uint8_t)model.wall_clock_sec;
    frame[pos++] = (uint8_t)(model.steps >> 24);
    frame[pos++] = (uint8_t)(model.steps >> 16);
    frame[pos++] = (uint8_t)(model.steps >> 8);
    frame[pos++] = (uint8_t)model.steps;
    frame[pos++] = (uint8_t)(model.distance_m >> 24);
    frame[pos++] = (uint8_t)(model.distance_m >> 16);
    frame[pos++] = (uint8_t)(model.distance_m >> 8);
    frame[pos++] = (uint8_t)model.distance_m;
    frame[pos++] = (uint8_t)(model.calories_dkcal >> 24);
    frame[pos++] = (uint8_t)(model.calories_dkcal >> 16);
    frame[pos++] = (uint8_t)(model.calories_dkcal >> 8);
    frame[pos++] = (uint8_t)model.calories_dkcal;
    memcpy(&frame[pos], model.firmware_version, version_len);
    pos += (uint16_t)version_len;

    PROTO_LOG("L2 MODEL snapshot battery=%u valid=%u charging=%u steps=%u fw=%s",
              (unsigned)model.battery_percent, (unsigned)model.battery_valid,
              (unsigned)model.charging, (unsigned)model.steps,
              model.firmware_version);
    (void)proto_send(frame, pos);
}

static void on_cmd_model(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0u; i < n; i++)
    {
        if (kvs[i].key == HMI_L2_MODEL_GET && kvs[i].val_len == 0u)
        {
            send_model_snapshot();
            return;
        }
    }
}

void hmi_l2_model_register(void)
{
    hmi_l2_register(HMI_L2_CMD_MODEL, on_cmd_model);
}
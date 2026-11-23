#include "watch_l2_cmd_factory.h"
#include "watch_l2.h"
#include "honey_protocol.h"
#include "honey_protocol_log.h"

#include "app_event.h"
#include "app_event_defs.h"
#include "flashdb_registry.h"

#include "honey_protocol_task.h"

#include <fal.h>
#include <flashdb.h>
#include <gap.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FACTORY_SN_KEY       "fac.sn"
#define FACTORY_PID_KEY      "fac.pid"
#define FACTORY_DEFAULTS_KEY "fac.def"
#define FACTORY_CONFIG_KEY   "fac.cfg"
#define FACTORY_TCAL_KEY     "fac.tcal"

#define FACTORY_RSP_MAX      128u

static bool s_aging;
static bool s_tp_test;
static bool s_opus;
static bool s_uv;

static bool send_factory(uint8_t key, const uint8_t *value, uint16_t length);

static void request_haptic(uint8_t action, uint8_t strength, uint16_t duration_ms)
{
    app_evt_haptic_request_t request =
    {
        .action = action,
        .strength = strength,
        .duration_ms = duration_ms,
    };
    if (app_event_publish(EVT_HAPTIC_REQUEST, &request, sizeof(request)) != 0)
    {
        HONEY_PROTOCOL_LOG("L2 FACTORY haptic request failed action=%u", action);
    }
}

static void on_power_voltage_response(app_event_id_t id, const void *payload,
                                      size_t len, void *user)
{
    (void)id;
    (void)user;
    if (payload == NULL || len != sizeof(app_evt_power_voltage_rsp_t))
    {
        return;
    }
    int16_t millivolts = ((const app_evt_power_voltage_rsp_t *)payload)->millivolts;
    uint8_t response[7] =
    {
        WATCH_L2_CMD_FACTORY, 0x00u, 0x04u, 0x00u, 0x02u,
        (uint8_t)((uint16_t)millivolts >> 8), (uint8_t)millivolts,
    };
    if (!honey_protocol_send_with_ack(response, sizeof(response)))
    {
        HONEY_PROTOCOL_LOG("L2 FACTORY battery response post failed");
    }
}

static void on_voice_prompt_response(app_event_id_t id, const void *payload,
                                     size_t len, void *user)
{
    (void)id;
    (void)user;
    uint8_t success = 0u;
    if (payload != NULL && len == sizeof(app_evt_voice_prompt_rsp_t))
    {
        success = ((const app_evt_voice_prompt_rsp_t *)payload)->success != 0u ? 1u : 0u;
    }
    uint8_t response[6] = {
        WATCH_L2_CMD_FACTORY, 0x00u, 0x2Bu, 0x00u, 0x01u, success,
    };
    if (!honey_protocol_send_with_ack(response, sizeof(response)))
    {
        HONEY_PROTOCOL_LOG("L2 FACTORY VP response post failed");
    }
}

static bool send_factory(uint8_t key, const uint8_t *value, uint16_t length)
{
    if (length > FACTORY_RSP_MAX)
    {
        return false;
    }
    uint8_t frame[5u + FACTORY_RSP_MAX];
    frame[0] = WATCH_L2_CMD_FACTORY;
    frame[1] = 0x00u;
    frame[2] = key;
    frame[3] = (uint8_t)(length >> 8);
    frame[4] = (uint8_t)length;
    if (length != 0u && value != NULL)
    {
        memcpy(&frame[5], value, length);
    }
    return honey_protocol_send_with_ack(frame, (uint16_t)(5u + length));
}

static bool kv_get(const char *key, void *out, size_t capacity, size_t *length)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL || out == NULL || capacity == 0u || length == NULL)
    {
        return false;
    }
    struct fdb_blob blob;
    size_t got = fdb_kv_get_blob(kvdb, key,
                                 fdb_blob_make(&blob, out, capacity));
    if (got == 0u || got > capacity)
    {
        return false;
    }
    *length = got;
    return true;
}

static bool kv_set(const char *key, const uint8_t *value, size_t length)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL || value == NULL || length == 0u)
    {
        return false;
    }
    struct fdb_blob blob;
    return fdb_kv_set_blob(kvdb, key,
                           fdb_blob_make(&blob, (void *)value, length)) == FDB_NO_ERR;
}

static bool ascii_value(const uint8_t *value, size_t length)
{
    if (value == NULL || length == 0u)
    {
        return false;
    }
    for (size_t i = 0u; i < length; i++)
    {
        if (value[i] < 0x20u || value[i] > 0x7Eu)
        {
            return false;
        }
    }
    return true;
}

static size_t default_sn(char *out, size_t capacity)
{
    uint8_t bd_addr[6] = {0};
    (void)gap_get_param(GAP_PARAM_BD_ADDR, bd_addr);
    int written = snprintf(out, capacity, "8773G%02X%02X%02X",
                           bd_addr[0], bd_addr[1], bd_addr[2]);
    return written > 0 && (size_t)written < capacity ? (size_t)written : 0u;
}

static void read_sn(void)
{
    uint8_t stored[16];
    size_t stored_length = 0u;
    if (kv_get(FACTORY_SN_KEY, stored, sizeof(stored), &stored_length) &&
        ascii_value(stored, stored_length))
    {
        (void)send_factory(0x2E, stored, (uint16_t)stored_length);
        return;
    }

    char generated[16];
    size_t generated_length = default_sn(generated, sizeof(generated));
    (void)send_factory(0x2E, (const uint8_t *)generated,
                       (uint16_t)generated_length);
}

static void read_pid(void)
{
    uint8_t pid[8];
    size_t length = 0u;
    if (!kv_get(FACTORY_PID_KEY, pid, sizeof(pid), &length) || length != sizeof(pid))
    {
        memcpy(pid, "RTL8773G", sizeof(pid));
    }
    (void)send_factory(0x4D, pid, sizeof(pid));
}

static void read_defaults(void)
{
    uint8_t value = 0u;
    size_t length = 0u;
    if (!kv_get(FACTORY_DEFAULTS_KEY, &value, sizeof(value), &length) || length != 1u)
    {
        value = 0u;
    }
    (void)send_factory(0x3E, &value, 1u);
}

static void read_config(void)
{
    uint8_t config[64];
    size_t length = 0u;
    if (kv_get(FACTORY_CONFIG_KEY, config, sizeof(config), &length) &&
        ascii_value(config, length))
    {
        (void)send_factory(0x54, config, (uint16_t)length);
    }
    else
    {
        (void)send_factory(0x54, NULL, 0u);
    }
}

static void read_history_addresses(void)
{
    uint8_t response[5u * 8u] = {0};
    const struct fal_partition *pedo = fal_partition_find("fdb_tsdb1");
    uint64_t address = pedo != NULL ?
                       (uint64_t)RTK_FLASH_START_ADDR + (uint64_t)pedo->offset : 0u;
    for (uint8_t i = 0u; i < 8u; i++)
    {
        response[i] = (uint8_t)(address >> (56u - i * 8u));
    }
    (void)send_factory(0xF8, response, sizeof(response));
}

static void read_temperature_calibration(void)
{
    uint8_t stored[2];
    uint8_t response[5] = {0};
    size_t length = 0u;
    if (kv_get(FACTORY_TCAL_KEY, stored, sizeof(stored), &length) && length == 2u)
    {
        response[0] = 1u;
        response[3] = stored[0];
        response[4] = stored[1];
    }
    (void)send_factory(0x3A, response, sizeof(response));
}

static void handle_factory_key(const watch_l2_kv_t *kv)
{
    HONEY_PROTOCOL_LOG("L2 FACTORY key=0x%02x val_len=%d", kv->key, kv->val_len);

    switch (kv->key)
    {
    case 0x03u:
        if (app_event_publish(EVT_POWER_VOLTAGE_REQ, NULL, 0u) != 0)
        {
            HONEY_PROTOCOL_LOG("L2 FACTORY battery voltage request failed");
        }
        break;

    case 0x20u:
        {
            const uint8_t version[4] = {1u, 2u, 3u, 100u};
            (void)send_factory(0x21, version, sizeof(version));
        }
        break;

    case 0x22u:
        {
            const uint8_t gsensor[9] = {0};
            (void)send_factory(0x23, gsensor, sizeof(gsensor));
        }
        break;

    case 0x26u:
        {
            const uint8_t outside = 0x01u;
            (void)send_factory(0x27, &outside, 1u);
        }
        break;

    case 0x28u:
        if (kv->val_len == 1u)
        {
            (void)kv_set(FACTORY_DEFAULTS_KEY, kv->val, 1u);
        }
        break;

    case 0x29u:
        if (kv->val_len == 1u)
        {
            if (kv->val[0] != 0u)
            {
                request_haptic(APP_HAPTIC_REQ_START, 60u, 0u);
            }
            else
            {
                request_haptic(APP_HAPTIC_REQ_STOP, 0u, 0u);
            }
        }
        break;

    case 0x2Au:
        {
            app_evt_voice_prompt_req_t request = {0};
            if (kv->val_len >= 1u)
            {
                request.index = kv->val[0];
            }
            if (kv->val_len >= 2u)
            {
                request.language = kv->val[1];
            }
            if (app_event_publish(EVT_VOICE_PROMPT_REQ,
                                  &request, sizeof(request)) != 0)
            {
                HONEY_PROTOCOL_LOG("L2 FACTORY VP request failed");
            }
        }
        break;

    case 0x2Du:
        read_sn();
        break;

    case 0x2Fu:
        if (kv->val_len == 2u && kv_set(FACTORY_TCAL_KEY, kv->val, 2u))
        {
            const uint8_t ok = 0x01u;
            (void)send_factory(0x30, &ok, 1u);
        }
        else
        {
            const uint8_t failed = 0x00u;
            (void)send_factory(0x30, &failed, 1u);
        }
        break;

    case 0x31u:
        if (kv->val_len == 1u && kv->val[0] != 0u)
        {
            app_health_measure_req_t request = APP_HEALTH_REQ_HR_START;
            if (app_event_publish(EVT_HEALTH_MEASURE_REQ,
                                  &request, sizeof(request)) != 0)
            {
                HONEY_PROTOCOL_LOG("L2 FACTORY HR start request failed");
            }
        }
        else
        {
            app_health_measure_req_t request = APP_HEALTH_REQ_HR_STOP;
            if (app_event_publish(EVT_HEALTH_MEASURE_REQ,
                                  &request, sizeof(request)) != 0)
            {
                HONEY_PROTOCOL_LOG("L2 FACTORY HR stop request failed");
            }
        }
        {
            const uint8_t leak[7] = {0};
            (void)send_factory(0x32, leak, sizeof(leak));
        }
        break;

    case 0x35u:
        {
            const uint8_t failed = 0x00u;
            (void)send_factory(0x36, &failed, 1u);
        }
        break;

    case 0x37u:
        {
            bool ok = kv->val_len > 0u && kv->val_len <= 16u &&
                      ascii_value(kv->val, kv->val_len) &&
                      kv_set(FACTORY_SN_KEY, kv->val, kv->val_len);
            const uint8_t status = ok ? 0x01u : 0x00u;
            (void)send_factory(0x38, &status, 1u);
        }
        break;

    case 0x39u:
        read_temperature_calibration();
        break;

    case 0x3Bu:
    case 0x3Cu:
        break;

    case 0x3Du:
        read_defaults();
        break;

    case 0x42u:
        s_tp_test = kv->val_len == 1u && kv->val[0] != 0u;
        break;

    case 0x44u:
        {
            const uint8_t license[56] = {0};
            (void)send_factory(0x45, license, sizeof(license));
        }
        break;

    case 0x46u:
        {
            const uint8_t failed = 0x00u;
            (void)send_factory(0x47, &failed, 1u);
        }
        break;

    case 0x48u:
        s_opus = kv->val_len == 1u && kv->val[0] != 0u;
        break;

    case 0x4Au:
        s_uv = kv->val_len == 1u && kv->val[0] != 0u;
        break;

    case 0x4Cu:
        read_pid();
        break;

    case 0x4Eu:
        {
            bool ok = kv->val_len == 8u &&
                      ascii_value(kv->val, kv->val_len) &&
                      kv_set(FACTORY_PID_KEY, kv->val, kv->val_len);
            const uint8_t status = ok ? 0x01u : 0x00u;
            (void)send_factory(0x4F, &status, 1u);
        }
        break;

    case 0x50u:
        {
            uint8_t result[28];
            memset(result, 0x02, sizeof(result));
            (void)send_factory(0x51, result, sizeof(result));
        }
        break;

    case 0x52u:
        if (kv->val_len <= 64u && ascii_value(kv->val, kv->val_len))
        {
            (void)kv_set(FACTORY_CONFIG_KEY, kv->val, kv->val_len);
        }
        break;

    case 0x53u:
        read_config();
        break;

    case 0x55u:
        {
            const uint8_t count = 0u;
            (void)send_factory(0x56, &count, 1u);
        }
        break;

    case 0x57u:
        break;

    case 0x58u:
        {
            const uint8_t failed = 0x00u;
            (void)send_factory(0x59, &failed, 1u);
        }
        break;

    case 0x5Au:
        {
            static const uint8_t debug[] = "RTL8773G factory ready";
            (void)send_factory(0x5B, debug, sizeof(debug) - 1u);
        }
        break;

    case 0xF2u:
    case 0xF3u:
    case 0xF4u:
        request_haptic(APP_HAPTIC_REQ_PULSE, 60u, 100u);
        break;

    case 0xF5u:
        s_aging = true;
        {
            const uint8_t aging = 0x01u;
            (void)send_factory(0xF6, &aging, 1u);
        }
        break;

    case 0xF7u:
        read_history_addresses();
        break;

    case 0xFAu:
        (void)send_factory(0xFB, NULL, 0u);
        break;

    case 0xFCu:
    case 0xFEu:
        /* Reset and power-off stay disabled until factory-control policy is specified. */
        break;

    default:
        HONEY_PROTOCOL_LOG("L2 FACTORY unsupported key=0x%02x", kv->key);
        break;
    }
}

static void on_cmd_factory(uint8_t version, const watch_l2_kv_t *kvs, uint8_t n)
{
    if (version != 0u)
    {
        HONEY_PROTOCOL_LOG("L2 FACTORY unsupported version=%u", version);
        return;
    }
    for (uint8_t i = 0u; i < n; i++)
    {
        handle_factory_key(&kvs[i]);
    }
}

void watch_l2_factory_register(void)
{
    watch_l2_register(WATCH_L2_CMD_FACTORY, on_cmd_factory);
    if (app_event_subscribe(EVT_POWER_VOLTAGE_RSP,
                            on_power_voltage_response, NULL) != 0)
    {
        HONEY_PROTOCOL_LOG("L2 FACTORY EVT_POWER_VOLTAGE_RSP subscribe failed");
    }
    if (app_event_subscribe(EVT_VOICE_PROMPT_RSP,
                            on_voice_prompt_response, NULL) != 0)
    {
        HONEY_PROTOCOL_LOG("L2 FACTORY EVT_VOICE_PROMPT_RSP subscribe failed");
    }
}

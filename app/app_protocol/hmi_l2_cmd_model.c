#include "hmi_l2_cmd_model.h"

#include "gui_port_sysval.h"
#include "gui_message.h"
#include "gui_sysval.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "hmi_protocal_task.h"
#include "proto_log.h"

#include <stdlib.h>
#include <string.h>

enum { MODEL_FIELDS = 7 };
static const char *const model_keys[MODEL_FIELDS] =
{
    GUI_SYSVAL_KEY_POWER_BATTERY_LEVEL,
    GUI_SYSVAL_KEY_POWER_BATTERY_CHARGING,
    WATCH_SYSVAL_KEY_EPOCH_SECONDS,
    GUI_SYSVAL_KEY_HEALTH_PEDOMETER_STEPS,
    GUI_SYSVAL_KEY_HEALTH_PEDOMETER_DISTANCE,
    WATCH_SYSVAL_KEY_CALORIES_DKCAL,
    GUI_SYSVAL_KEY_DEVICE_FIRMWARE_VERSION,
};

typedef struct
{
    unsigned index;
    uint8_t flags;
    uint8_t battery;
    uint32_t epoch;
    uint32_t steps;
    uint32_t distance;
    uint32_t calories_dkcal;
    char version[GUI_SYSVAL_STR_MAX];
} model_reply_t;

static void put_u32(uint8_t *frame, uint16_t *pos, uint32_t value)
{
    frame[(*pos)++] = (uint8_t)(value >> 24);
    frame[(*pos)++] = (uint8_t)(value >> 16);
    frame[(*pos)++] = (uint8_t)(value >> 8);
    frame[(*pos)++] = (uint8_t)value;
}

static void send_model_snapshot(model_reply_t *reply)
{
    /* Schema v2: flags, battery, wall-clock epoch, steps, metres, 0.1 kcal, version. */
    uint8_t frame[2u + 3u + 20u + GUI_SYSVAL_STR_MAX];
    uint16_t pos = 0u;
    size_t version_len = strnlen(reply->version, sizeof(reply->version));

    frame[pos++] = HMI_L2_CMD_MODEL;
    frame[pos++] = 0x00u;
    frame[pos++] = HMI_L2_MODEL_SNAPSHOT;
    frame[pos++] = 0x00u;
    frame[pos++] = (uint8_t)(20u + version_len);
    frame[pos++] = HMI_L2_MODEL_SCHEMA_V2;
    frame[pos++] = reply->flags;
    frame[pos++] = reply->battery;
    frame[pos++] = (uint8_t)version_len;
    put_u32(frame, &pos, reply->epoch);
    put_u32(frame, &pos, reply->steps);
    put_u32(frame, &pos, reply->distance);
    put_u32(frame, &pos, reply->calories_dkcal);
    memcpy(&frame[pos], reply->version, version_len);
    pos += (uint16_t)version_len;
    (void)proto_send(frame, pos);
    free(reply);
}

static void model_send_on_l2(l2_msg_t *message)
{
    send_model_snapshot(message->u.buf);
}

static void model_next(model_reply_t *reply);

static void model_response(gui_sysval_request_id_t id, gui_sysval_status_t status,
                           const gui_sysval_value_t *value, void *user_data)
{
    model_reply_t *reply = user_data;
    unsigned field = reply->index++;
    (void)id;
    if (status == GUI_SYSVAL_STATUS_OK && value != NULL)
    {
        switch (field)
        {
        case 0:
            if (value->data.i >= 0 && value->data.i <= 100)
            {
                reply->battery = (uint8_t)value->data.i;
                reply->flags |= HMI_L2_MODEL_FLAG_BATTERY_VALID;
            }
            break;
        case 1:
            if (value->data.b) { reply->flags |= HMI_L2_MODEL_FLAG_CHARGING; }
            break;
        case 2:
            if (value->data.i > 0)
            {
                reply->epoch = (uint32_t)value->data.i;
                reply->flags |= HMI_L2_MODEL_FLAG_TIME_VALID;
            }
            break;
        case 3: reply->steps = (uint32_t)value->data.i; break;
        case 4: reply->distance = (uint32_t)value->data.i; break;
        case 5: reply->calories_dkcal = (uint32_t)value->data.i; break;
        case 6: memcpy(reply->version, value->data.s, sizeof(reply->version)); break;
        }
    }
    if (field == 1 && !(reply->flags & HMI_L2_MODEL_FLAG_BATTERY_VALID))
    {
        reply->flags &= ~HMI_L2_MODEL_FLAG_CHARGING;
    }
    model_next(reply);
}

static void model_next(model_reply_t *reply)
{
    if (reply->index == MODEL_FIELDS)
    {
        if (!hmi_proto_post_call(model_send_on_l2, reply))
        {
            PROTO_LOG("L2 MODEL response queue full");
            free(reply);
        }
        return;
    }
    if (gui_sysval_get_request(model_keys[reply->index], model_response, reply) ==
        GUI_SYSVAL_REQUEST_INVALID)
    {
        PROTO_LOG("L2 MODEL sysval request failed index=%u", reply->index);
        free(reply);
    }
}

static void model_on_gui(void *message)
{
    gui_msg_t *msg = message;
    model_reply_t *reply = msg->payload;
    model_next(reply);
}

static void on_cmd_model(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0u; i < n; i++)
    {
        if (kvs[i].key == HMI_L2_MODEL_GET && kvs[i].val_len == 0u)
        {
            model_reply_t *reply = calloc(1, sizeof(*reply));
            if (reply == NULL) { return; }
            gui_msg_t msg = { .event = GUI_EVENT_USER_DEFINE,
                              .cb = model_on_gui, .payload = reply
                            };
            if (!gui_send_msg_to_server(&msg))
            {
                PROTO_LOG("L2 MODEL GUI queue full");
                free(reply);
            }
            return;
        }
    }
}

void hmi_l2_model_register(void)
{
    hmi_l2_register(HMI_L2_CMD_MODEL, on_cmd_model);
}
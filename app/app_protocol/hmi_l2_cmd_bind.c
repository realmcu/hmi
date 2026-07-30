#include "hmi_l2_cmd_bind.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

#include "app_event.h"
#include "app_event_defs.h"

#include <stdbool.h>

#define BIND_USER_ID_LEN       32u
#define BIND_STATUS_SUCCESS    0x00u
#define BIND_STATUS_FAILED     0x01u

static bool send_bind_response(uint8_t status)
{
    const uint8_t rsp[] =
    {
        HMI_L2_CMD_BIND,
        0x00u,
        HMI_L2_BIND_RSP,
        0x00u, 0x01u,
        status,
    };

    return proto_send(rsp, sizeof(rsp));
}

static void on_cmd_bind(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 BIND    key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);

        if (kvs[i].key == HMI_L2_UNBIND)
        {
            int rc = app_event_publish(EVT_USER_UNBOUND, NULL, 0);
            if (rc != 0)
            {
                PROTO_LOG("L2 BIND failed to publish EVT_USER_UNBOUND rc=%d", rc);
            }
            return;
        }

        if (kvs[i].key != HMI_L2_BIND_REQ)
        {
            continue;
        }

        uint8_t status = BIND_STATUS_SUCCESS;
        if (kvs[i].val == NULL || kvs[i].val_len != BIND_USER_ID_LEN)
        {
            status = BIND_STATUS_FAILED;
            PROTO_LOG("L2 BIND invalid user ID length=%d", kvs[i].val_len);
        }

        bool sent = send_bind_response(status);
        PROTO_LOG("L2 BIND response status=0x%02x sent=%d", status, sent);

        /* Publish EVT_USER_BOUND only on a successful handshake. The
         * event carries no payload — subscribers that need the user ID
         * read it from KVDB (TODO: still needs to be persisted from here
         * into a well-known KV key once app_setting exposes a setter). */
        if (status == BIND_STATUS_SUCCESS)
        {
            int rc = app_event_publish(EVT_USER_BOUND, NULL, 0);
            if (rc != 0)
            {
                PROTO_LOG("L2 BIND failed to publish EVT_USER_BOUND rc=%d", rc);
            }
        }
        return;
    }
}

void hmi_l2_bind_register(void)
{
    hmi_l2_register(HMI_L2_CMD_BIND, on_cmd_bind);
}

#include "hmi_l2_cmd_bind.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

#include "app_event.h"
#include "app_event_defs.h"

#include <flashdb.h>
#include "flashdb_registry.h"

#include <stdbool.h>
#include <string.h>

#define BIND_USER_ID_LEN       32u
#define BIND_STATUS_SUCCESS    0x00u
#define BIND_STATUS_FAILED     0x01u

#define BIND_USER_ID_KV_KEY    "bind.user_id"

static bool send_bind_status(uint8_t key, uint8_t status)
{
    const uint8_t rsp[] =
    {
        HMI_L2_CMD_BIND,
        0x00u,
        key,
        0x00u, 0x01u,
        status,
    };
    return proto_send(rsp, sizeof(rsp));
}

static bool valid_user_id(const uint8_t *value, uint16_t len)
{
    if (value == NULL || len != BIND_USER_ID_LEN)
    {
        return false;
    }
    for (uint16_t i = 0; i < len; i++)
    {
        const uint8_t c = value[i];
        /* HoneyProtocol defines 32B ASCII, zero-padded when shorter. */
        if (!((c >= 0x20u && c <= 0x7Eu) || c == 0x00u))
        {
            return false;
        }
    }
    return true;
}

static bool load_user_id(uint8_t out[BIND_USER_ID_LEN])
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL)
    {
        return false;
    }

    struct fdb_blob blob;
    size_t got = fdb_kv_get_blob(
        kvdb, BIND_USER_ID_KV_KEY,
        fdb_blob_make(&blob, out, BIND_USER_ID_LEN));
    return got == BIND_USER_ID_LEN;
}

static bool save_user_id(const uint8_t *value)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb == NULL)
    {
        return false;
    }

    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(
        kvdb, BIND_USER_ID_KV_KEY,
        fdb_blob_make(&blob, (void *)value, BIND_USER_ID_LEN));
    return err == FDB_NO_ERR;
}

static void delete_user_id(void)
{
    fdb_kvdb_t kvdb = flashdb_registry_get_env_kvdb();
    if (kvdb != NULL)
    {
        (void)fdb_kv_del(kvdb, BIND_USER_ID_KV_KEY);
    }
}

static void publish_user_bound(void)
{
    int rc = app_event_publish(EVT_USER_BOUND, NULL, 0);
    if (rc != 0)
    {
        PROTO_LOG("L2 BIND failed to publish EVT_USER_BOUND rc=%d", rc);
    }
}

static void handle_login(const hmi_l2_kv_t *kv)
{
    uint8_t stored[BIND_USER_ID_LEN];
    bool ok = valid_user_id(kv->val, kv->val_len) &&
              load_user_id(stored) &&
              memcmp(stored, kv->val, BIND_USER_ID_LEN) == 0;
    uint8_t status = ok ? BIND_STATUS_SUCCESS : BIND_STATUS_FAILED;
    (void)send_bind_status(HMI_L2_LOGIN_RSP, status);
    PROTO_LOG("L2 BIND login status=0x%02x", status);
    if (ok)
    {
        publish_user_bound();
    }
}

static void handle_bind(const hmi_l2_kv_t *kv)
{
    uint8_t status = valid_user_id(kv->val, kv->val_len) &&
                     save_user_id(kv->val)
                         ? BIND_STATUS_SUCCESS
                         : BIND_STATUS_FAILED;
    (void)send_bind_status(HMI_L2_BIND_RSP, status);
    PROTO_LOG("L2 BIND bind status=0x%02x", status);
    if (status == BIND_STATUS_SUCCESS)
    {
        publish_user_bound();
    }
}

static void on_cmd_bind(uint8_t version, const hmi_l2_kv_t *kvs, uint8_t n)
{
    if (version != 0u)
    {
        PROTO_LOG("L2 BIND unsupported version=%u", (unsigned)version);
        return;
    }

    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 BIND key=0x%02x val_len=%d",
                  kvs[i].key, kvs[i].val_len);

        switch (kvs[i].key)
        {
        case HMI_L2_LOGIN_REQ:
            handle_login(&kvs[i]);
            return;
        case HMI_L2_BIND_REQ:
            handle_bind(&kvs[i]);
            return;
        case HMI_L2_UNBIND:
        {
            delete_user_id();
            int rc = app_event_publish(EVT_USER_UNBOUND, NULL, 0);
            if (rc != 0)
            {
                PROTO_LOG("L2 BIND failed to publish EVT_USER_UNBOUND rc=%d", rc);
            }
            return;
        }
        default:
            break;
        }
    }
}

void hmi_l2_bind_register(void)
{
    hmi_l2_register(HMI_L2_CMD_BIND, on_cmd_bind);
}

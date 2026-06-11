#include "hmi_l2_cmd_notify.h"
#include "protocol/hmi_l2.h"
#include "protocol/proto_log.h"

static void on_cmd_notify(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 NOTIFY  key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

void hmi_l2_notify_register(void)
{
    hmi_l2_register(HMI_L2_CMD_NOTIFY, on_cmd_notify);
}

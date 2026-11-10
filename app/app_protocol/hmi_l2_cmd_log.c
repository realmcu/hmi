#include "hmi_l2_cmd_log.h"
#include "hmi_l2.h"
#include "proto_log.h"

static void on_cmd_log(uint8_t version, const hmi_l2_kv_t *kvs, uint8_t n)
{
    (void)version;
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 LOG     key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

void hmi_l2_log_register(void)
{
    hmi_l2_register(HMI_L2_CMD_LOG, on_cmd_log);
}

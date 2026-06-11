#include "hmi_l2_cmd_factory.h"
#include "protocol/hmi_l2.h"
#include "protocol/proto_log.h"

static void on_cmd_factory(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 FACTORY key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

void hmi_l2_factory_register(void)
{
    hmi_l2_register(HMI_L2_CMD_FACTORY, on_cmd_factory);
}

#include "hmi_l2_cmd_sport.h"
#include "hmi_l2.h"
#include "proto_log.h"

static void on_cmd_sport(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        PROTO_LOG("L2 SPORT   key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

void hmi_l2_sport_register(void)
{
    hmi_l2_register(HMI_L2_CMD_SPORT, on_cmd_sport);
}

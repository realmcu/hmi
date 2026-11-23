#include "watch_l2_cmd_control.h"
#include "watch_l2.h"
#include "honey_protocol_log.h"

static void on_cmd_control(uint8_t version, const watch_l2_kv_t *kvs, uint8_t n)
{
    (void)version;
    for (uint8_t i = 0; i < n; i++)
    {
        HONEY_PROTOCOL_LOG("L2 CONTROL key=0x%02x val_len=%d", kvs[i].key, kvs[i].val_len);
    }
}

void watch_l2_control_register(void)
{
    watch_l2_register(WATCH_L2_CMD_CONTROL, on_cmd_control);
}

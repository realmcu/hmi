#if 0// TODO: for zephyr build
#include "guidef.h"
#include "gui_api.h"
#include "gui_port.h"
#include <stdio.h>
#if (SUPPORT_NAND_FLASH == 1)
#include "fmc_api_ext.h"
#endif

int port_ftl_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
#if (SUPPORT_NAND_FLASH == 1)
//    int ret = fmc_flash_nand_read(addr, buf, len);
//    if (ret)
//    {
//        return 0;
//    }
//    else
//    {
//        gui_log("port_ftl_read fail! %d", ret);
//        return -1;
//    }
#else
    return -1;
#endif
}

int port_ftl_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    gui_log("port_ftl_write not support");
    return -1;
}

int port_ftl_erase(uint32_t addr, uint32_t len)
{
    gui_log("port_ftl_erase not support");
    return -1;
}

static struct gui_ftl ftl_port =
{
    .read      = (int (*)(uint32_t addr, uint8_t *buf, uint32_t len))port_ftl_read,
    .write     = (int (*)(uint32_t addr, const uint8_t *buf, uint32_t len))port_ftl_write,
    .erase     = (int (*)(uint32_t addr, uint32_t len))port_ftl_erase,
};

extern void gui_ftl_info_register(struct gui_ftl *info);

void gui_port_ftl_init(void)
{
    gui_ftl_info_register(&ftl_port);
}
#endif
void gui_port_ftl_init(void)
{
    return;
}


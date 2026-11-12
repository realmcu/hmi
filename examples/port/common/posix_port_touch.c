/* ================================================================
 * Touch 层：当前板选型（决定 /dev/touch0 挂到哪颗芯片）
 *
 * 本文件不含硬件访问逻辑，做两件事：
 *
 *  1. 提供 posix_touch_bind / unbind —— 为 touch 应用层保留一个带类型
 *     前缀的 API。实现只是转发到框架层的 posix_device_bind_alias
 *     （见 core/posix_device.c），gsensor 等其它多型号外设走同一机制。
 *
 *  2. 在 POSIX_INIT_APP_EXPORT 阶段调一次 bind，把当前板对应的芯片挂成
 *     /dev/touch0。上层应用只 open /dev/touch0，不关心底下是 CST816D
 *     还是 CHSC6417；调试仍可直接 open /dev/cst816d。
 *
 * 换板/换芯片只改本文件的 #if 分支（或加 Kconfig 项）：
 *     CONFIG_BOARD_TOUCH_CST816D  (默认，对齐 eBadge 板)
 *     CONFIG_BOARD_TOUCH_CHSC6417
 * 两颗芯片的驱动 .c 与 posix 框架都不动。
 *
 * init 段优先级说明：
 *   芯片驱动通过 POSIX_INIT_DEVICE_EXPORT 位于 .posix$init1，
 *   本文件的 board_touch_init 位于 .posix$init2（APP_EXPORT），
 *   由链接脚本保证 bind 发生在芯片自身注册之后。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_touch.h"
#include <zephyr/sys/printk.h>

#define TOUCH_CST816D_CHIP_ID   0xB6

/* ---------- 带类型前缀的薄壳（转发到框架层通用 alias 机制） ---------- */

int posix_touch_bind(const char *alias, const char *chip_path)
{
    return posix_device_bind_alias(alias, chip_path);
}

int posix_touch_unbind(const char *alias)
{
    return posix_device_unbind_alias(alias);
}


static int board_touch_init(void)
{
    posix_fd_t probe = posix_open("/dev/cst816d");
    int ret;

    if (probe == POSIX_FD_NULL)
    {
        printk("[TOUCH] open /dev/cst816d failed\n");
        return POSIX_ERR_NODEV;
    }

    uint32_t chip_id = 0;
    ret = posix_ioctl(probe, POSIX_TOUCH_IOCTL_GET_CHIP_ID, &chip_id);
    posix_close(probe);

    if (ret != POSIX_OK)
    {
        printk("[TOUCH] CST816D read id failed: %d\n", ret);
        return ret;
    }

    printk("[TOUCH] CST816D id: 0x%08X\n", chip_id);

    if (chip_id == TOUCH_CST816D_CHIP_ID)
    {
        return posix_touch_bind("/dev/touch0", "/dev/cst816d");
    }

    printk("[TOUCH] CST816D id mismatch\n");
    return POSIX_ERR_NODEV;
}
POSIX_INIT_POST_DEVICE_EXPORT(board_touch_init);

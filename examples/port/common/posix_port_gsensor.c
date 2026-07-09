/* ================================================================
 * Gsensor 层：当前板选型（决定 /dev/gsensor0 挂到哪颗芯片）
 *
 * 结构与 posix_port_touch.c 完全对称，做两件事：
 *
 *  1. 提供 posix_gsensor_bind / unbind —— 带类型前缀的薄壳，转发到
 *     框架层通用 posix_device_bind_alias（见 core/posix_device.c）。
 *
 *  2. 在 POSIX_INIT_APP_EXPORT 阶段调一次 bind，把当前板对应的芯片挂成
 *     /dev/gsensor0。上层应用只 open /dev/gsensor0，不关心底下是哪颗；
 *     调试仍可直接 open 芯片型号路径（当前是 /dev/sc7a20）。
 *
 * 芯片本身的驱动在同目录 posix_port_sc7a20.c，注册 /dev/sc7a20。
 *
 * 换板/换芯片只改本文件的 #if 分支（或加 Kconfig 项）。未来加型号时：
 *   1) 新增 posix_port_bma253.c 之类，POSIX_INIT_DEVICE_EXPORT 注册
 *      /dev/bma253
 *   2) 本文件加一个 #elif 分支
 * 已有芯片驱动 .c 与 posix 框架都不动。
 *
 * init 段优先级：
 *   芯片驱动 POSIX_INIT_DEVICE_EXPORT 位于 .posix$init1，
 *   本文件 board_gsensor_init 位于 .posix$init2（APP_EXPORT），
 *   保证 bind 发生在芯片自身注册之后。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_gsensor.h"

/* ---------- 带类型前缀的薄壳（转发到框架层通用 alias 机制） ---------- */

int posix_gsensor_bind(const char *alias, const char *chip_path)
{
    return posix_device_bind_alias(alias, chip_path);
}

int posix_gsensor_unbind(const char *alias)
{
    return posix_device_unbind_alias(alias);
}

/* ---------- 当前板选型 ---------- */

#if defined(CONFIG_BOARD_GSENSOR_BMA253)
#  define BOARD_GSENSOR_CHIP_PATH  "/dev/bma253"
#elif defined(CONFIG_BOARD_GSENSOR_LIS3DH)
#  define BOARD_GSENSOR_CHIP_PATH  "/dev/lis3dh"
#else   /* 默认 SC7A20，对齐 eBadge 板硬件 */
#  define BOARD_GSENSOR_CHIP_PATH  "/dev/sc7a20"
#endif

static int board_gsensor_init(void)
{
    return posix_gsensor_bind("/dev/gsensor0", BOARD_GSENSOR_CHIP_PATH);
}
POSIX_INIT_APP_EXPORT(board_gsensor_init);

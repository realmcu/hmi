#ifndef POSIX_H
#define POSIX_H

/* ================================================================
 * POSIX 设备抽象层 — 统一入口
 *
 * 用法：
 *   #include "posix.h"
 *
 *   所有需要的外设 ioctl 头文件按需包含：
 *   #include "ioctls/posix_ioctl_uart.h"
 *
 * 换芯片/换 OS 时，不需要修改此文件
 * 只需要替换 port/ 目录下的后端实现
 * ================================================================ */

#include "posix_types.h"
#include "posix_device.h"

/* ioctl 头文件按需 include，不在 posix.h 统一 include
 * 因为不是所有项目都会用到全部 6 种外设
 *
 * 推荐在项目自己的 device_cfg.h 中按需包含：
 *
 *   #include "posix.h"
 *   #include "ioctls/posix_ioctl_uart.h"
 *   #include "ioctls/posix_ioctl_spi.h"
 *   // ... 其他需要的外设
 */

#endif /* POSIX_H */

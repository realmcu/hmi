#ifndef POSIX_IOCTL_H
#define POSIX_IOCTL_H

#include "posix_types.h"

/* ================================================================
 * ioctl cmd 编码方案
 *
 * 编码规则（类似 Linux，但精简以适应嵌入式环境）：
 *
 *   高位                          低位
 *   +----------------+-----------+
 *   |  magic (8 bit) | nr (8 bit) |
 *   +----------------+-----------+
 *
 * magic: 设备类型标识，每个设备类型分配一个
 * nr:    命令序号，同类型设备内唯一
 *
 * 宏：
 *   POSIX_IOC(magic, nr)        — 构造 cmd
 *   POSIX_IOC_MAGIC(cmd)        — 提取设备类型
 *   POSIX_IOC_NR(cmd)           — 提取命令序号
 *
 * magic 值由 posix_device.h 中的 POSIX_DEVICE_MAGIC_XXX 定义
 * ================================================================ */

#define POSIX_IOC(magic, nr)     ((int)((((magic) & 0xFF) << 8) | ((nr) & 0xFF)))
#define POSIX_IOC_MAGIC(cmd)     (((cmd) >> 8) & 0xFF)
#define POSIX_IOC_NR(cmd)        ((cmd) & 0xFF)

#endif /* POSIX_IOCTL_H */

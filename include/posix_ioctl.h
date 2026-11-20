#ifndef POSIX_IOCTL_H
#define POSIX_IOCTL_H

#include "posix_types.h"

/* ================================================================
 * ioctl cmd encoding scheme
 *
 * Encoding rules (similar to Linux, but simplified for embedded environments):
 *
 *   high bits                      low bits
 *   +----------------+-----------+
 *   |  magic (8 bit) | nr (8 bit) |
 *   +----------------+-----------+
 *
 * magic: device type identifier, one per device type
 * nr:    command sequence number, unique within a device type
 *
 * Macros:
 *   POSIX_IOC(magic, nr)        - build cmd
 *   POSIX_IOC_MAGIC(cmd)        - extract device type
 *   POSIX_IOC_NR(cmd)           - extract command number
 *
 * magic values are defined by POSIX_DEVICE_MAGIC_XXX in posix_device.h
 * ================================================================ */

#define POSIX_IOC(magic, nr)     ((int)((((magic) & 0xFF) << 8) | ((nr) & 0xFF)))
#define POSIX_IOC_MAGIC(cmd)     (((cmd) >> 8) & 0xFF)
#define POSIX_IOC_NR(cmd)        ((cmd) & 0xFF)

#endif /* POSIX_IOCTL_H */

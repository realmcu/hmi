#ifndef POSIX_H
#define POSIX_H

/* ================================================================
 * POSIX device abstraction layer — unified entry point
 *
 * Usage:
 *   #include "posix.h"
 *
 *   Include the ioctl headers for the peripherals you need:
 *   #include "ioctls/posix_ioctl_uart.h"
 *
 * Switching chip or OS requires no change to this file,
 * only replacing the backend implementation under port/
 * ================================================================ */

#include "posix_types.h"
#include "posix_device.h"

/* ioctl headers are included on demand, not pulled in by posix.h,
 * because not every project uses all 6 peripheral types
 *
 * Recommended: include what you need in your project's device_cfg.h:
 *
 *   #include "posix.h"
 *   #include "ioctls/posix_ioctl_uart.h"
 *   #include "ioctls/posix_ioctl_spi.h"
 *   // ... other peripherals you need
 */

#endif /* POSIX_H */

/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <stdbool.h>

/* The CMake/GCC build force-includes build/.../include/generated/config.h
 * through its precompiled header, so every Kconfig symbol is visible in every
 * translation unit. The Keil project has neither that header nor its include
 * path, hence the board's Kconfig selection is mirrored here for the symbols
 * the sources test with #ifdef. Keep in sync with Kconfig. */
#ifndef CONFIG_REALTEK_APP_NAVI_PROJECTION
#define CONFIG_REALTEK_APP_NAVI_PROJECTION 1
#endif

#ifndef   __WEAK
#define __WEAK                                 __attribute__((weak))
#endif

#ifndef   __weak
#define __weak                                 __attribute__((weak))
#endif


#ifndef   __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE                   __attribute__((always_inline)) static __inline
#endif

#ifndef   __forceinline
#define __forceinline                   __attribute__((always_inline)) __inline
#endif

#ifndef   __STATIC_ALWAYS_INLINE
#define __STATIC_ALWAYS_INLINE  static inline __attribute__ ((always_inline))
#endif


#ifndef RAM_TYPE_DATA_ON
#define RAM_TYPE_DATA_ON OS_MEM_TYPE_DATA
#endif


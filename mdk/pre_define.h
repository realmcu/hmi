/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

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


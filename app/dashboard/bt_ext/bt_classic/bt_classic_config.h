/*
 * Copyright (c) 2026 Realtek Semiconductor Corporation. All rights reserved.
 *
 * Classic BT (BR/EDR) per-profile compile switches + Class of Device.
 */

#ifndef __BT_CLASSIC_CONFIG_H__
#define __BT_CLASSIC_CONFIG_H__

/*
 * Per-profile enable switches. Each profile is gated independently across
 * init + event dispatch + SDP record.
 *
 * This header maps Kconfig options (CONFIG_BT_CLASSIC_ENABLE_*) to the
 * module-local BT_CLASSIC_ENABLE_* macros. When Kconfig is not used, set
 * the macros externally via compiler flags or by editing this file.
 */
#ifndef BT_CLASSIC_ENABLE_A2DP
#ifdef CONFIG_BT_CLASSIC_ENABLE_A2DP
#define BT_CLASSIC_ENABLE_A2DP    1
#else
#define BT_CLASSIC_ENABLE_A2DP    0
#endif
#endif

#ifndef BT_CLASSIC_ENABLE_AVRCP
#ifdef CONFIG_BT_CLASSIC_ENABLE_AVRCP
#define BT_CLASSIC_ENABLE_AVRCP   1
#else
#define BT_CLASSIC_ENABLE_AVRCP   0
#endif
#endif

#ifndef BT_CLASSIC_ENABLE_HFP
#ifdef CONFIG_BT_CLASSIC_ENABLE_HFP
#define BT_CLASSIC_ENABLE_HFP     1
#else
#define BT_CLASSIC_ENABLE_HFP     0
#endif
#endif

#ifndef BT_CLASSIC_ENABLE_SPP
#ifdef CONFIG_BT_CLASSIC_ENABLE_SPP
#define BT_CLASSIC_ENABLE_SPP     1
#else
#define BT_CLASSIC_ENABLE_SPP     0
#endif
#endif

#ifndef BT_CLASSIC_ENABLE_HID
#ifdef CONFIG_BT_CLASSIC_ENABLE_HID
#define BT_CLASSIC_ENABLE_HID     1
#else
#define BT_CLASSIC_ENABLE_HID     0
#endif
#endif

#if !BT_CLASSIC_ENABLE_A2DP && !BT_CLASSIC_ENABLE_AVRCP && \
    !BT_CLASSIC_ENABLE_HFP && !BT_CLASSIC_ENABLE_SPP && !BT_CLASSIC_ENABLE_HID
#error "enable at least one classic BT profile in bt_classic_config.h"
#endif

/* COD signals one identity: audio (A2DP/AVRCP/HFP) or HID-only. */
#if BT_CLASSIC_ENABLE_HID && \
    !BT_CLASSIC_ENABLE_A2DP && !BT_CLASSIC_ENABLE_AVRCP && !BT_CLASSIC_ENABLE_HFP
#define BT_CLASSIC_CLASS_OF_DEVICE   0x000540  /* Peripheral/Keyboard (HID) */
#else
#define BT_CLASSIC_CLASS_OF_DEVICE   0x0C025A  /* Audio */
#endif

#endif /* __BT_CLASSIC_CONFIG_H__ */

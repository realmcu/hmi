/*
 * Copyright(c) 2025, Realtek Semiconductor Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __GSENSOR_SC7A20_H__
#define __GSENSOR_SC7A20_H__

#include <stdint.h>
#include <stdbool.h>
#include "rtl876x.h"
#include "rtl876x_rcc.h"
#include "rtl876x_pinmux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- I2C Pins / Bus ----------------
 * SC7A20H is on I2C0: I2C1 is taken by AW87390 amp, I2C2 is taken by touch.
 * Board-level wiring: SCL=P3_5 / SDA=P3_4 (RTL87x3 pinmux can route any digital
 * pad as I2C0_CLK/I2C0_DAT, so it is fine if they differ from board.h defaults).
 */
#define GSENSOR_I2C_SCL              P3_5
#define GSENSOR_I2C_SDA              P3_4

#define GSENSOR_I2C_BUS              I2C0
#define GSENSOR_I2C_FUNC_SCL         I2C0_CLK
#define GSENSOR_I2C_FUNC_SDA         I2C0_DAT
#define GSENSOR_I2C_APBPeriph        APBPeriph_I2C0
#define GSENSOR_I2C_APBClock         APBPeriph_I2C0_CLOCK

/* 7-bit slave address: SA0/SDO high → 0x19, low → 0x18.
 * init probes both addresses and automatically selects the one matching WHO_AM_I. */
#define GSENSOR_I2C_ADDR_HIGH        0x19
#define GSENSOR_I2C_ADDR_LOW         0x18

/* Expected WHO_AM_I (0x0F) value. SC7A20/SC7A20H = 0x11; LIS2DH-compatible part = 0x33. */
#define GSENSOR_CHIP_ID              0x11

/**
 * @brief  Power-on init: configure I2C0 pins and controller, probe device address, write working registers.
 *         No-interrupt mode (pure polling read).
 */
void gsensor_sc7a20_init(void);

/**
 * @brief  Read chip ID (WHO_AM_I, 0x0F).
 * @param  p_id  Output, read ID value.
 * @retval true  I2C read OK (raw value is returned as-is; caller validates).
 */
bool gsensor_sc7a20_read_id(uint8_t *p_id);

/**
 * @brief  Read 3-axis raw acceleration (left-aligned int16, normal mode high 10 bits valid).
 * @param  x/y/z  Output, raw count per axis; at ±2g, (raw>>6) is approx 4mg/count.
 * @retval true   Read succeeded.
 */
bool gsensor_sc7a20_read_xyz(int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif /* __GSENSOR_SC7A20_H__ */

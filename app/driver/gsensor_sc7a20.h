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

/* ---------------- I2C 引脚 / 总线 ----------------
 * SC7A20H 挂在 I2C0:I2C1 已被 AW87390 功放占用、I2C2 被 touch 占用。
 * 引脚为板级实际接线 SCL=P3_5 / SDA=P3_4(RTL87x3 pinmux 可将任意数字
 * pad 路由为 I2C0_CLK/I2C0_DAT,与 board.h 中 I2C0 的建议引脚不同无妨)。
 */
#define GSENSOR_I2C_SCL              P3_5
#define GSENSOR_I2C_SDA              P3_4

#define GSENSOR_I2C_BUS              I2C0
#define GSENSOR_I2C_FUNC_SCL         I2C0_CLK
#define GSENSOR_I2C_FUNC_SDA         I2C0_DAT
#define GSENSOR_I2C_APBPeriph        APBPeriph_I2C0
#define GSENSOR_I2C_APBClock         APBPeriph_I2C0_CLOCK

/* 7-bit 从地址:SA0/SDO 接高 → 0x19,接低 → 0x18。
 * init 会先后探测这两个地址并自动选用匹配 WHO_AM_I 的那个。 */
#define GSENSOR_I2C_ADDR_HIGH        0x19
#define GSENSOR_I2C_ADDR_LOW         0x18

/* WHO_AM_I(0x0F)期望值。SC7A20/SC7A20H 为 0x11;若换 LIS2DH 兼容片为 0x33。 */
#define GSENSOR_CHIP_ID              0x11

/**
 * @brief  开机初始化:配置 I2C0 引脚与控制器、探测器件地址、写工作寄存器。
 *         无中断模式(纯轮询读取)。
 */
void gsensor_sc7a20_init(void);

/**
 * @brief  读芯片 ID(WHO_AM_I, 0x0F)。
 * @param  p_id  输出,读到的 ID。
 * @retval true  I2C 读成功(不校验内容,内容由调用方判断)。
 */
bool gsensor_sc7a20_read_id(uint8_t *p_id);

/**
 * @brief  读取三轴加速度原始值(左对齐 int16,normal 模式高 10 位有效)。
 * @param  x/y/z  输出,各轴原始计数;@±2g 时 (raw>>6) 约 4mg/count。
 * @retval true   读取成功。
 */
bool gsensor_sc7a20_read_xyz(int16_t *x, int16_t *y, int16_t *z);

#ifdef __cplusplus
}
#endif

#endif /* __GSENSOR_SC7A20_H__ */

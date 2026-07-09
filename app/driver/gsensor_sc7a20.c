/*
 * Copyright(c) 2025, Realtek Semiconductor Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SC7A20H 3-axis accelerometer driver (I2C0, no interrupt, polling read).
 * Registers are compatible with ST LIS2DH / LIS3DH.
 * Reference: https://github.com/GoodbyeLina/test-sc7a20h
 */
#include "rtl876x_rcc.h"
#include "rtl876x_pinmux.h"
#include "rtl876x_i2c.h"
#include "platform_utils.h"
#include "trace.h"
#include "gsensor_sc7a20.h"


/* ---------------- SC7A20H Registers ---------------- */
#define SC7A20_REG_WHO_AM_I     0x0F
#define SC7A20_REG_CTRL_REG1    0x20
#define SC7A20_REG_CTRL_REG4    0x23
#define SC7A20_REG_OUT_X_L      0x28   /* 0x28~0x2D: X/Y/Z each L, H, total 6 bytes */

/* Multi-byte read: set sub-address bit 7 to enable address auto-increment */
#define SC7A20_AUTO_INCREMENT   0x80

/* CTRL_REG1 = 0x57: ODR=100Hz, Normal mode, Z/Y/X enable */
#define SC7A20_CTRL1_VAL        0x57
/* CTRL_REG4 = 0x80: BDU=1 (block update, avoids hi/low byte tearing on read), FS=±2g, little-endian */
#define SC7A20_CTRL4_VAL        0x80

/* Currently active 7-bit slave address, determined by init probe */
static uint8_t s_slave_addr = GSENSOR_I2C_ADDR_HIGH;

/* ---------------- Low-level I2C Read / Write ---------------- */
static bool gsensor_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    I2C_SetSlaveAddress(GSENSOR_I2C_BUS, s_slave_addr);
    if (I2C_MasterWrite(GSENSOR_I2C_BUS, buf, 2) != I2C_Success)
    {
        DBG_DIRECT("[sc7a20] write reg 0x%02x fail", reg);
        return false;
    }
    return true;
}

static bool gsensor_read_regs(uint8_t reg, uint8_t *p_data, uint8_t len)
{
    uint8_t sub = (len > 1) ? (uint8_t)(reg | SC7A20_AUTO_INCREMENT) : reg;
    I2C_SetSlaveAddress(GSENSOR_I2C_BUS, s_slave_addr);
    /* Write sub-address + repeated-start read, no STOP in between */
    if (I2C_RepeatRead(GSENSOR_I2C_BUS, &sub, 1, p_data, len) != I2C_Success)
    {
        DBG_DIRECT("[sc7a20] read reg 0x%02x fail", reg);
        return false;
    }
    return true;
}

/* ---------------- I2C0 Controller / Pin Initialization ---------------- */
static void gsensor_i2c_hw_init(void)
{
    Pad_PullConfigValue(GSENSOR_I2C_SCL, 1);
    Pad_PullConfigValue(GSENSOR_I2C_SDA, 1);

    Pinmux_Config(GSENSOR_I2C_SCL, GSENSOR_I2C_FUNC_SCL);
    Pinmux_Config(GSENSOR_I2C_SDA, GSENSOR_I2C_FUNC_SDA);

    Pad_Config(GSENSOR_I2C_SCL, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE,
               PAD_OUT_LOW);
    Pad_Config(GSENSOR_I2C_SDA, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE,
               PAD_OUT_LOW);

    RCC_PeriphClockCmd(GSENSOR_I2C_APBPeriph, GSENSOR_I2C_APBClock, DISABLE);
    RCC_PeriphClockCmd(GSENSOR_I2C_APBPeriph, GSENSOR_I2C_APBClock, ENABLE);

    I2C_InitTypeDef I2C_InitStructure;
    I2C_StructInit(&I2C_InitStructure);
    I2C_InitStructure.I2C_Clock        = 40000000;
    I2C_InitStructure.I2C_ClockSpeed   = 400000;
    I2C_InitStructure.I2C_DeviveMode   = I2C_DeviveMode_Master;
    I2C_InitStructure.I2C_AddressMode  = I2C_AddressMode_7BIT;
    I2C_InitStructure.I2C_Ack          = I2C_Ack_Enable;
    I2C_Init(GSENSOR_I2C_BUS, &I2C_InitStructure);
    I2C_Cmd(GSENSOR_I2C_BUS, ENABLE);
}

bool gsensor_sc7a20_read_id(uint8_t *p_id)
{
    if (!p_id)
    {
        return false;
    }
    return gsensor_read_regs(SC7A20_REG_WHO_AM_I, p_id, 1);
}

/* Probe 0x19 / 0x18, pick address matching WHO_AM_I; return false if neither matches */
static bool gsensor_probe(void)
{
    const uint8_t addrs[2] = {GSENSOR_I2C_ADDR_HIGH, GSENSOR_I2C_ADDR_LOW};
    for (uint8_t i = 0; i < 2; i++)
    {
        uint8_t id = 0;
        s_slave_addr = addrs[i];
        if (gsensor_sc7a20_read_id(&id))
        {
            DBG_DIRECT("[sc7a20] probe addr 0x%02x -> WHO_AM_I=0x%02x", s_slave_addr, id);
            if (id == GSENSOR_CHIP_ID)
            {
                return true;
            }
        }
    }
    return false;
}

void gsensor_sc7a20_init(void)
{
    DBG_DIRECT("[sc7a20] init");
    gsensor_i2c_hw_init();
    platform_delay_ms(10);

    if (!gsensor_probe())
    {
        DBG_DIRECT("[sc7a20] chip NOT found, check wiring/addr; fallback 0x%02x",
                   GSENSOR_I2C_ADDR_HIGH);
        s_slave_addr = GSENSOR_I2C_ADDR_HIGH;   /* Still write registers to facilitate I2C waveform capture */
    }

    gsensor_write_reg(SC7A20_REG_CTRL_REG1, SC7A20_CTRL1_VAL);
    gsensor_write_reg(SC7A20_REG_CTRL_REG4, SC7A20_CTRL4_VAL);
    platform_delay_ms(10);

    DBG_DIRECT("[sc7a20] init done, addr=0x%02x", s_slave_addr);
}

bool gsensor_sc7a20_read_xyz(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t data[6] = {0};

    if (!x || !y || !z)
    {
        return false;
    }
    if (!gsensor_read_regs(SC7A20_REG_OUT_X_L, data, 6))
    {
        return false;
    }
    /* Little-endian: low byte first. Raw 16-bit left-aligned, normal mode high 10 bits valid */
    *x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
    *y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
    *z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
    return true;
}

/* ================================================================
 * I2C 控制器 POSIX 端口（RTL87x3G / RTL876x 原生驱动直调）
 *
 * 路径：/dev/i2c0
 *
 * 该文件直接调用 rtl876x_i2c.h / rtl876x_pinmux.h / rtl876x_rcc.h
 * 提供的原生驱动 API（I2C_Init / I2C_MasterWrite / I2C_RepeatRead ...），
 * 而不是过 hw_* 间接钩子。定位对齐 Zephyr 板级 I²C driver 的
 * 常见做法：POSIX 层做上层抽象，底层直吃 SDK。
 *
 * 板级引脚（与 board/evb/eBadge/app/driver/gsensor_sc7a20.c 一致）：
 *   I2C0 SCL = P3_5   SDA = P3_4
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_i2c.h"
#include <stdbool.h>
#include <string.h>

#include "rtl876x.h"
#include "rtl876x_rcc.h"
#include "rtl876x_pinmux.h"
#include "rtl876x_i2c.h"

/* ---------------- I²C 控制器配置 ---------------- */
typedef struct
{
    int                unit;         /* 0 → /dev/i2c0 */
    I2C_TypeDef       *bus;          /* 硬件基址 */
    uint8_t            pin_scl;
    uint8_t            pin_sda;
    uint8_t            func_scl;     /* Pinmux 功能号 */
    uint8_t            func_sda;
    uint32_t           apb_periph;
    uint32_t           apb_clock;
    uint32_t           default_hz;
    uint8_t            hw_inited;
    uint8_t            cur_slave;    /* 缓存以避免每次都写 SlaveAddress */
} i2c_drv_t;

typedef struct
{
    i2c_drv_t          *drv;
    posix_i2c_config_t  cfg;
    int                 in_use;
} i2c_file_t;

#define MAX_I2C_FILES   4
static i2c_file_t s_i2c_files[MAX_I2C_FILES];

/* ---------------- 硬件初始化：pin + RCC + I2C_Init ---------------- */
static bool i2c_hw_init(i2c_drv_t *drv, uint32_t speed_hz, uint8_t addr_bits)
{
    /* Pad 上拉，进入 pinmux 模式 */
    Pad_PullConfigValue(drv->pin_scl, 1);
    Pad_PullConfigValue(drv->pin_sda, 1);
    Pinmux_Config(drv->pin_scl, drv->func_scl);
    Pinmux_Config(drv->pin_sda, drv->func_sda);
    Pad_Config(drv->pin_scl, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP,
               PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(drv->pin_sda, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP,
               PAD_OUT_DISABLE, PAD_OUT_LOW);

    /* 上电时钟：先关后开，保证寄存器复位 */
    RCC_PeriphClockCmd(drv->apb_periph, drv->apb_clock, DISABLE);
    RCC_PeriphClockCmd(drv->apb_periph, drv->apb_clock, ENABLE);

    /* 控制器：Master 模式、7 / 10 bit 寻址、开 ACK */
    I2C_InitTypeDef cfg;
    I2C_StructInit(&cfg);
    cfg.I2C_Clock        = 40000000;      /* 内部时钟 40 MHz */
    cfg.I2C_ClockSpeed   = speed_hz;
    cfg.I2C_DeviveMode   = I2C_DeviveMode_Master;
    cfg.I2C_AddressMode  = (addr_bits == POSIX_I2C_ADDR_10BIT)
                           ? I2C_AddressMode_10BIT : I2C_AddressMode_7BIT;
    cfg.I2C_Ack          = I2C_Ack_Enable;
    I2C_Init(drv->bus, &cfg);
    I2C_Cmd(drv->bus, ENABLE);
    return true;
}

/* ---------------- I²C 事务包装 ---------------- */
static bool i2c_hw_write(i2c_drv_t *drv, uint16_t addr,
                         const uint8_t *buf, size_t len)
{
    if (addr != drv->cur_slave)
    {
        I2C_SetSlaveAddress(drv->bus, addr);
        drv->cur_slave = (uint8_t)addr;
    }
    return I2C_MasterWrite(drv->bus, (uint8_t *)buf, (uint16_t)len) == I2C_Success;
}

static bool i2c_hw_read(i2c_drv_t *drv, uint16_t addr,
                        uint8_t *buf, size_t len)
{
    if (addr != drv->cur_slave)
    {
        I2C_SetSlaveAddress(drv->bus, addr);
        drv->cur_slave = (uint8_t)addr;
    }
    return I2C_MasterRead(drv->bus, buf, (uint16_t)len) == I2C_Success;
}

static bool i2c_hw_write_then_read(i2c_drv_t *drv, uint16_t addr,
                                   const uint8_t *wbuf, size_t wlen,
                                   uint8_t *rbuf,        size_t rlen)
{
    if (addr != drv->cur_slave)
    {
        I2C_SetSlaveAddress(drv->bus, addr);
        drv->cur_slave = (uint8_t)addr;
    }
    return I2C_RepeatRead(drv->bus, (uint8_t *)wbuf, (uint16_t)wlen,
                          rbuf, (uint16_t)rlen) == I2C_Success;
}

/* ---------------- POSIX 驱动接口 ---------------- */
static void *i2c_open(void *d, const char *p)
{
    (void)p;
    i2c_drv_t  *drv = (i2c_drv_t *)d;
    i2c_file_t *f   = NULL;

    for (int i = 0; i < MAX_I2C_FILES; i++)
    {
        if (!s_i2c_files[i].in_use)
        {
            s_i2c_files[i].in_use = 1;
            f = &s_i2c_files[i];
            break;
        }
    }
    if (!f) { return POSIX_OPEN_ERR; }

    f->drv = drv;
    f->cfg.speed_hz  = drv->default_hz;
    f->cfg.addr_bits = POSIX_I2C_ADDR_7BIT;

    if (!drv->hw_inited)
    {
        if (!i2c_hw_init(drv, f->cfg.speed_hz, f->cfg.addr_bits))
        {
            f->in_use = 0;
            return POSIX_OPEN_ERR;
        }
        drv->hw_inited  = 1;
        drv->cur_slave  = 0xFF;   /* 强制第一次事务写 SlaveAddress */
    }
    return f;
}

static int i2c_close(void *d, void *fv)
{
    (void)d;
    i2c_file_t *f = (i2c_file_t *)fv;
    if (f) { f->in_use = 0; f->drv = NULL; }
    return POSIX_OK;
}

/* I2C 不走数据流，read/write 恒不支持 */
static posix_ssize_t i2c_read(void *d, void *f, void *b, size_t c)
{ (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP; }

static posix_ssize_t i2c_write(void *d, void *f, const void *b, size_t c)
{ (void)d; (void)f; (void)b; (void)c; return POSIX_ERR_NOSUPP; }

/* 构造子地址 byte 数组（最多 2 字节，大端） */
static int build_subaddr(const posix_i2c_msg_t *m, uint8_t out[2])
{
    if (m->reg_len == 0) { return 0; }
    if (m->reg_len == 1) { out[0] = (uint8_t)m->reg; return 1; }
    if (m->reg_len == 2) { out[0] = (uint8_t)(m->reg >> 8); out[1] = (uint8_t)m->reg; return 2; }
    return -1;
}

static int i2c_ioctl(void *d, void *fv, unsigned long cmd, void *arg)
{
    (void)d;
    i2c_file_t *file = (i2c_file_t *)fv;
    i2c_drv_t  *drv  = file->drv;

    switch (cmd)
    {
    case POSIX_I2C_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_config_t *cfg = (posix_i2c_config_t *)arg;
            /* 只有频率或寻址位数变了才重 init（避免不必要的 pin 重配置） */
            if (cfg->speed_hz != file->cfg.speed_hz ||
                cfg->addr_bits != file->cfg.addr_bits)
            {
                if (!i2c_hw_init(drv, cfg->speed_hz, cfg->addr_bits))
                {
                    return POSIX_ERR_IO;
                }
                drv->cur_slave = 0xFF;
            }
            file->cfg = *cfg;
            return POSIX_OK;
        }

    case POSIX_I2C_IOCTL_GET_CONFIG:
        if (!arg) { return POSIX_ERR_INVAL; }
        *(posix_i2c_config_t *)arg = file->cfg;
        return POSIX_OK;

    case POSIX_I2C_IOCTL_WRITE_REG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;
            uint8_t sub[2]; int slen = build_subaddr(m, sub);
            if (slen < 0) { return POSIX_ERR_INVAL; }

            /* 子地址 + payload 串成单一事务，避免中间 STOP */
            uint8_t stack[16];
            size_t  total = (size_t)slen + m->len;
            if (total > sizeof(stack)) { return POSIX_ERR_NOMEM; }
            if (slen)   { memcpy(stack, sub, (size_t)slen); }
            if (m->len) { memcpy(stack + slen, m->buf, m->len); }
            return i2c_hw_write(drv, m->addr, stack, total) ? POSIX_OK : POSIX_ERR_IO;
        }

    case POSIX_I2C_IOCTL_READ_REG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;
            uint8_t sub[2]; int slen = build_subaddr(m, sub);
            if (slen < 0) { return POSIX_ERR_INVAL; }
            if (slen == 0)
            {
                return i2c_hw_read(drv, m->addr, m->buf, m->len)
                       ? POSIX_OK : POSIX_ERR_IO;
            }
            return i2c_hw_write_then_read(drv, m->addr, sub, (size_t)slen,
                                          m->buf, m->len)
                   ? POSIX_OK : POSIX_ERR_IO;
        }

    case POSIX_I2C_IOCTL_RAW_WRITE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;
            return i2c_hw_write(drv, m->addr, m->buf, m->len)
                   ? POSIX_OK : POSIX_ERR_IO;
        }

    case POSIX_I2C_IOCTL_RAW_READ:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;
            return i2c_hw_read(drv, m->addr, m->buf, m->len)
                   ? POSIX_OK : POSIX_ERR_IO;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

const posix_driver_ops_t g_i2c_ops =
{
    .open  = i2c_open,
    .close = i2c_close,
    .read  = i2c_read,
    .write = i2c_write,
    .ioctl = i2c_ioctl,
};

/* ---------- 设备实例 + 自动注册 ---------- */
static i2c_drv_t s_i2c0 =
{
    .unit       = 0,
    .bus        = I2C0,
    .pin_scl    = P3_5,
    .pin_sda    = P3_4,
    .func_scl   = I2C0_CLK,
    .func_sda   = I2C0_DAT,
    .apb_periph = APBPeriph_I2C0,
    .apb_clock  = APBPeriph_I2C0_CLOCK,
    .default_hz = POSIX_I2C_SPEED_FAST,
    .hw_inited  = 0,
    .cur_slave  = 0xFF,
};

static int i2c_init(void)
{
    return posix_device_register("/dev/i2c0", &g_i2c_ops, &s_i2c0);
}
POSIX_INIT_DEVICE_EXPORT(i2c_init);

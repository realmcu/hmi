/* ================================================================
 * I2C 驱动 — 基于 Zephyr I2C API 的 posix-device 适配层
 *
 * 路径格式: /dev/i2c0, /dev/i2c1
 *   /dev/i2c0  → i2c0（DT_NODELABEL(i2c0)）
 *   /dev/i2c1  → i2c1（DT_NODELABEL(i2c1)）
 *
 * 所有 I2C 事务统一走 ioctl，read/write 返回 POSIX_ERR_NOSUPP。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_i2c.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <string.h>

/* ---------- 控制器私有数据（每个 posix 设备一份） ---------- */
typedef struct
{
    const struct device *dev;   /* Zephyr I2C device */
} i2c_drv_data_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct
{
    bool               in_use;
    i2c_drv_data_t    *drv;
    posix_i2c_config_t cfg;
} i2c_file_t;

#define MAX_I2C_FILES  4
static i2c_file_t s_i2c_files[MAX_I2C_FILES];

/* ---------- speed_hz → Zephyr I2C speed code ---------- */
static uint32_t hz_to_zephyr_speed(uint32_t hz)
{
    if (hz <= POSIX_I2C_SPEED_STANDARD)  { return I2C_SPEED_STANDARD; }
    if (hz <= POSIX_I2C_SPEED_FAST)      { return I2C_SPEED_FAST;     }
    if (hz <= POSIX_I2C_SPEED_FAST_PLUS) { return I2C_SPEED_FAST_PLUS; }
    return I2C_SPEED_HIGH;
}

/* ---------- open ---------- */
static void *i2c_open(void *drv_data, const char *path)
{
    (void)path;
    i2c_drv_data_t *d = (i2c_drv_data_t *)drv_data;

    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    i2c_file_t *f = NULL;
    for (int i = 0; i < MAX_I2C_FILES; i++)
    {
        if (!s_i2c_files[i].in_use)
        {
            f = &s_i2c_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use       = true;
            f->drv          = d;
            /* 默认配置：400 kHz，7-bit 地址 */
            f->cfg.speed_hz  = POSIX_I2C_SPEED_FAST;
            f->cfg.addr_bits = POSIX_I2C_ADDR_7BIT;
            break;
        }
    }
    return f ? f : POSIX_OPEN_ERR;
}

/* ---------- close ---------- */
static int i2c_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    i2c_file_t *f = (i2c_file_t *)file_priv;
    if (f && f->in_use)
    {
        f->in_use = false;
        f->drv    = NULL;
    }
    return POSIX_OK;
}

/* ---------- read：I2C 无流式语义 ---------- */
static posix_ssize_t i2c_read_fn(void *drv_data, void *file_priv,
                                 void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- write：I2C 无流式语义 ---------- */
static posix_ssize_t i2c_write_fn(void *drv_data, void *file_priv,
                                  const void *buf, size_t count)
{
    (void)drv_data;
    (void)file_priv;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int i2c_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    i2c_file_t *f = (i2c_file_t *)file_priv;

    switch (cmd)
    {
    case POSIX_I2C_IOCTL_SET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_config_t *cfg = (posix_i2c_config_t *)arg;

            uint32_t speed = hz_to_zephyr_speed(cfg->speed_hz);
            uint32_t zephyr_cfg = I2C_SPEED_SET(speed) | I2C_MODE_CONTROLLER;

            int ret = i2c_configure(f->drv->dev, zephyr_cfg);
            if (ret < 0) { return POSIX_ERR_IO; }

            f->cfg = *cfg;
            return POSIX_OK;
        }

    case POSIX_I2C_IOCTL_GET_CONFIG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            *(posix_i2c_config_t *)arg = f->cfg;
            return POSIX_OK;
        }

    case POSIX_I2C_IOCTL_WRITE_REG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;

            if (m->reg_len == 0)
            {
                /* reg_len=0：等同 RAW_WRITE */
                int ret = i2c_write(f->drv->dev, m->buf, (uint32_t)m->len,
                                    m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }

            /* 把 reg bytes + data 合并到一次传输（两段，无 STOP 间隔） */
            uint8_t reg_buf[2];
            if (m->reg_len == 1)
            {
                reg_buf[0] = (uint8_t)(m->reg & 0xFF);
            }
            else
            {
                /* reg_len == 2：大端字节序 */
                reg_buf[0] = (uint8_t)((m->reg >> 8) & 0xFF);
                reg_buf[1] = (uint8_t)(m->reg & 0xFF);
            }

            struct i2c_msg msgs[2];
            msgs[0].buf   = reg_buf;
            msgs[0].len   = m->reg_len;
            msgs[0].flags = I2C_MSG_WRITE;

            msgs[1].buf   = m->buf;
            msgs[1].len   = (uint32_t)m->len;
            msgs[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

            if (f->cfg.addr_bits == POSIX_I2C_ADDR_10BIT)
            {
                msgs[0].flags |= I2C_MSG_ADDR_10_BITS;
                msgs[1].flags |= I2C_MSG_ADDR_10_BITS;
            }

            int ret = i2c_transfer(f->drv->dev, msgs, 2, m->addr);
            return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
        }

    case POSIX_I2C_IOCTL_READ_REG:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;

            if (m->reg_len == 0)
            {
                /* reg_len=0：等同 RAW_READ */
                int ret = i2c_read(f->drv->dev, m->buf, (uint32_t)m->len,
                                   m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }

            /* repeated-start：先写 reg bytes，再读 data */
            uint8_t reg_buf[2];
            if (m->reg_len == 1)
            {
                reg_buf[0] = (uint8_t)(m->reg & 0xFF);
            }
            else
            {
                reg_buf[0] = (uint8_t)((m->reg >> 8) & 0xFF);
                reg_buf[1] = (uint8_t)(m->reg & 0xFF);
            }

            if (f->cfg.addr_bits == POSIX_I2C_ADDR_10BIT)
            {
                struct i2c_msg msgs[2];
                msgs[0].buf   = reg_buf;
                msgs[0].len   = m->reg_len;
                msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_ADDR_10_BITS;

                msgs[1].buf   = m->buf;
                msgs[1].len   = (uint32_t)m->len;
                msgs[1].flags = I2C_MSG_READ | I2C_MSG_STOP | I2C_MSG_RESTART
                                | I2C_MSG_ADDR_10_BITS;

                int ret = i2c_transfer(f->drv->dev, msgs, 2, m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }
            else
            {
                int ret = i2c_write_read(f->drv->dev, m->addr,
                                         reg_buf, m->reg_len,
                                         m->buf,  m->len);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }
        }

    case POSIX_I2C_IOCTL_RAW_WRITE:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;

            if (f->cfg.addr_bits == POSIX_I2C_ADDR_10BIT)
            {
                struct i2c_msg msg;
                msg.buf   = m->buf;
                msg.len   = (uint32_t)m->len;
                msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP | I2C_MSG_ADDR_10_BITS;

                int ret = i2c_transfer(f->drv->dev, &msg, 1, m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }
            else
            {
                int ret = i2c_write(f->drv->dev, m->buf, (uint32_t)m->len,
                                    m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }
        }

    case POSIX_I2C_IOCTL_RAW_READ:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_i2c_msg_t *m = (posix_i2c_msg_t *)arg;

            if (f->cfg.addr_bits == POSIX_I2C_ADDR_10BIT)
            {
                struct i2c_msg msg;
                msg.buf   = m->buf;
                msg.len   = (uint32_t)m->len;
                msg.flags = I2C_MSG_READ | I2C_MSG_STOP | I2C_MSG_ADDR_10_BITS;

                int ret = i2c_transfer(f->drv->dev, &msg, 1, m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }
            else
            {
                int ret = i2c_read(f->drv->dev, m->buf, (uint32_t)m->len,
                                   m->addr);
                return (ret < 0) ? POSIX_ERR_IO : POSIX_OK;
            }
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_i2c_ops =
{
    .open  = i2c_open,
    .close = i2c_close,
    .read  = i2c_read_fn,
    .write = i2c_write_fn,
    .ioctl = i2c_ioctl,
};

/* ---------- 设备实例 ---------- */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c0), okay)
static i2c_drv_data_t s_i2c0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(i2c0)) };
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay)
static i2c_drv_data_t s_i2c1 = { .dev = DEVICE_DT_GET(DT_NODELABEL(i2c1)) };
#endif

/* ---------- 自动注册 ---------- */
static int i2c_init(void)
{
    int ret = POSIX_OK;
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c0), okay)
    ret = posix_device_register("/dev/i2c0", &g_i2c_ops, &s_i2c0);
    if (ret) { return ret; }
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay)
    ret = posix_device_register("/dev/i2c1", &g_i2c_ops, &s_i2c1);
#endif
    return ret;
}
POSIX_INIT_DEVICE_EXPORT(i2c_init);

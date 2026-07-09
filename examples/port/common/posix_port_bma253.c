/* ================================================================
 * BMA253 gsensor POSIX port —— STUB / PLACEHOLDER
 *
 * 这是一个占位驱动，目的是给"未来加第 N 颗 gsensor"提供一份可以直接
 * 照抄改的骨架。当前默认【不参与编译】—— 需要 Kconfig
 *   CONFIG_POSIX_PORT_GSENSOR_BMA253=y
 * 才会被编入。参考实现文件：posix_port_sc7a20.c。
 *
 * ---- 结构与 sc7a20 完全对称 ----
 *   POSIX_INIT_DEVICE_EXPORT 注册 /dev/bma253 —— 芯片型号路径
 *   通过 posix_port_gsensor.c 的板级选型挂成 /dev/gsensor0
 *
 * ---- 硬件事实（Bosch BMA253 datasheet 摘要）----
 *   I2C 地址：SDO 接 GND -> 0x18；SDO 接 VDDIO -> 0x19（跟 SC7A20 巧合一致）
 *   CHIP_ID 寄存器 0x00 应读回 0xFA
 *   量程寄存器 0x0F PMU_RANGE：2G=0x03, 4G=0x05, 8G=0x08, 16G=0x0C
 *   带宽寄存器 0x10 PMU_BW：7.81Hz~1000Hz 各档
 *   数据寄存器 0x02..0x07 三轴 low/high；每轴低字节 bit0 为 new_data 标志
 *   INT 寄存器 0x19 INT_EN_1 = 0x10 (data-ready)，需路由到 0x1A INT_MAP_1
 *
 * ---- 接手清单（实测硬件前必做） ----
 *   [ ] 用示波器/万用表确认 SDO 引脚接法，据此定 s_bma253_0.i2c_addr
 *   [ ] 确认 INT1 引脚连到哪个 GPIO，填 int_pin_path；不用中断则填 NULL
 *   [ ] 确认 I2C 总线是 i2c0 还是 i2c1（当前项目 gsensor 都在 i2c0）
 *   [ ] 把下面 TODO 段落实现完；范围/ODR 换算表照 datasheet 填
 *   [ ] Kconfig 打开 CONFIG_POSIX_PORT_GSENSOR_BMA253
 *   [ ] Kconfig 打开 CONFIG_BOARD_GSENSOR_BMA253（让 posix_port_gsensor.c 挂它）
 *   [ ] 跑 `posix_gsensor poll /dev/bma253` 直连验证再切 alias
 * ================================================================ */

#ifdef CONFIG_POSIX_PORT_GSENSOR_BMA253

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_gsensor.h"
#include "ioctls/posix_ioctl_i2c.h"
#include <stdbool.h>
#include <string.h>

/* ---------- BMA253 register map（datasheet §7） ---------- */
#define BMA253_ADDR_LOW     0x18   /* SDO -> GND */
#define BMA253_ADDR_HIGH    0x19   /* SDO -> VDDIO */
#define BMA253_REG_CHIP_ID  0x00
#define BMA253_CHIP_ID      0xFA

/* ---------- driver private data ---------- */
typedef struct
{
    int         unit;
    uint8_t     i2c_addr;
    uint8_t     probed;
    const char *i2c_path;
    const char *int_pin_path;
} bma253_drv_t;

typedef struct
{
    bma253_drv_t          *drv;
    posix_gsensor_config_t cfg;
    posix_fd_t             i2c_fd;
} bma253_file_t;

#define BMA253_MAX_FILES  1
static bma253_file_t s_bma253_files[BMA253_MAX_FILES];
static int           s_bma253_file_used[BMA253_MAX_FILES];

/* ---------- ops（全部 TODO；当前 stub 返回 NOSUPP） ---------- */

static void *bma253_open(void *drv_data, const char *path)
{
    (void)path;
    /* TODO:
     *   1. 从 s_bma253_files 里分配一个未用槽位
     *   2. posix_open(drv->i2c_path) 拿 I2C fd
     *   3. probe：读 CHIP_ID 应为 0xFA；否则 return POSIX_OPEN_ERR
     *   4. 如 drv->int_pin_path 非 NULL，posix_open + SET_IRQ + 创信号量
     */
    (void)drv_data;
    return POSIX_OPEN_ERR;   /* stub */
}

static int bma253_close(void *drv_data, void *file_priv)
{
    /* TODO: 释放 I2C fd / GPIO fd / 信号量，标记槽位空闲 */
    (void)drv_data; (void)file_priv;
    return POSIX_OK;
}

static posix_ssize_t bma253_read(void *drv_data, void *file_priv,
                                 void *buf, size_t count)
{
    /* TODO:
     *   IRQ 模式：posix_sem_take(drdy) 等中断
     *   读 0x02..0x07 六字节，按 datasheet 拼三轴 int16 + shift
     *   按 sensitivity 换成 mg 填 posix_gsensor_axis_t
     */
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t bma253_write(void *drv_data, void *file_priv,
                                  const void *buf, size_t count)
{
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

static int bma253_ioctl(void *drv_data, void *file_priv,
                        unsigned long cmd, void *arg)
{
    /* TODO: 处理 SET_CONFIG / GET_CONFIG / SET_POWER /
     *       SELF_TEST / READ_TEMP / SET_TIMEOUT */
    (void)drv_data; (void)file_priv; (void)cmd; (void)arg;
    return POSIX_ERR_NOSUPP;
}

static const posix_driver_ops_t g_bma253_ops =
{
    .open  = bma253_open,
    .close = bma253_close,
    .read  = bma253_read,
    .write = bma253_write,
    .ioctl = bma253_ioctl,
};

static bma253_drv_t s_bma253_0 =
{
    .unit         = 0,
    .i2c_addr     = BMA253_ADDR_LOW,   /* 待确认，见接手清单 */
    .probed       = 0,
    .i2c_path     = "/dev/i2c0",       /* 待确认 */
    .int_pin_path = NULL,              /* 轮询模式；接 INT1 时填路径 */
};

/* ---------- auto-registration ----------
 *
 * 注册路径是芯片型号 /dev/bma253。application 层通过 posix_port_gsensor.c
 * 的板级选型看到 /dev/gsensor0，不感知型号切换。
 */
static int bma253_init(void)
{
    return posix_device_register("/dev/bma253", &g_bma253_ops, &s_bma253_0);
}
POSIX_INIT_DEVICE_EXPORT(bma253_init);

#endif /* CONFIG_POSIX_PORT_GSENSOR_BMA253 */

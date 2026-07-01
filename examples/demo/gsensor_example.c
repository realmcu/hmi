// Zephyr Shell test: uart:~$ posix_gsensor [poll|irq]
/* ================================================================
 * G-sensor 使用示例（含轮询 & 中断两种模式）
 *
 * 数据通路完全走 POSIX 抽象：
 *   /dev/gsensor0 内部再 posix_open(/dev/i2c0) + posix_open(/dev/gpio0/pX)
 *   应用层只需要打开 gsensor 就够。
 *
 * 模式选择：
 *   posix_gsensor_config_t.use_irq = 0  → 轮询：posix_read 不阻塞
 *   posix_gsensor_config_t.use_irq = 1  → 中断：DRDY 上升沿唤醒 posix_read
 *
 * !!! 当前实现状态（本工程 link 的是 custom-rtos 端口） !!!
 *   posix_port_i2c.c      — RTL876x 原生 I2C API 落地（真跑 SC7A20）
 *   posix_port_gsensor.c  — 完整逻辑（SC7A20 全套寄存器 + IRQ 阻塞 read）
 *   posix_port_gpio.c     — 仍为 stub。因此 use_irq=1 时 SET_IRQ 会返回 OK，
 *                           但 DRDY 中断实际不会触发，posix_read 会一直超时。
 *                           要用 IRQ 模式，需先把 GPIO 端口落地（Zephyr GPIO
 *                           subsystem 或 RTL876x 原生 API 二选一）。
 *
 *   zephyr-rtk 端口保持独立 stub 副本，不影响本工程构建。
 *
 * 编译要求：需要 posix.h + posix_ioctl_gsensor.h
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_gsensor.h"

/* ----------------- 轮询模式（最简） ----------------- */
void example_gsensor_poll(void)
{
    /* === 1. 打开 === */
    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (!gs) { return; }

    /* === 2. 配置量程 ±2G、100Hz、轮询 === */
    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_2G,
        .odr_hz    = 100,
        .low_power = 0,
        .use_irq   = 0,
    };
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    /* === 3. 读三轴加速度（mg） === */
    posix_gsensor_axis_t accel;
    posix_read(gs, &accel, sizeof(accel));

    /* === 4. 自检（读 WHO_AM_I 校验） === */
    int ret = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SELF_TEST, NULL);
    /* ret == 0 表示正常 */

    /* === 5. 关闭 === */
    posix_close(gs);
    (void)accel; (void)ret;
}

/* ----------------- 中断模式（推荐用于低占用读取） -----------------
 * 流程：
 *   SET_CONFIG{use_irq=1} 内部会：
 *     - posix_open /dev/gpio0/pX 拿 DRDY 引脚
 *     - 创建信号量、posix_ioctl(SET_IRQ + ENABLE_IRQ)
 *     - 写 SC7A20 CTRL_REG3，把 DRDY 路由到 INT1
 *   posix_read 会阻塞在内部信号量上，直到 ISR 触发 give。
 */
void example_gsensor_irq(void)
{
    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (!gs) { return; }

    /* 1. 开中断模式：±4G、50Hz */
    posix_gsensor_config_t cfg =
    {
        .range     = POSIX_GSENSOR_RANGE_4G,
        .odr_hz    = 50,
        .low_power = 0,
        .use_irq   = 1,
    };
    if (posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg) != 0)
    {
        posix_close(gs);
        return;
    }

    /* 2. 设置 DRDY 等待超时（默认 1000ms） */
    uint32_t tmo_ms = 500;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_TIMEOUT, &tmo_ms);

    /* 3. 连续读 16 次 —— 每次 read 都阻塞到下一个数据就绪中断 */
    for (int i = 0; i < 16; i++)
    {
        posix_gsensor_axis_t accel;
        posix_ssize_t n = posix_read(gs, &accel, sizeof(accel));
        if (n < 0)
        {
            /* n == POSIX_ERR_TIMEOUT (-3) 表示中断超时 */
            break;
        }
        /* TODO: process accel.x/y/z (mg) */
    }

    /* 4. 切回轮询模式（teardown IRQ） */
    cfg.use_irq = 0;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    posix_close(gs);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include "posix_port.h"
#include <string.h>

static bool s_gs_inited = false;

static int do_gsensor_poll(const struct shell *sh)
{
    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (gs == POSIX_FD_NULL) { shell_error(sh, "open /dev/gsensor0 failed"); return -1; }

    posix_gsensor_config_t cfg =
    {
        .range = POSIX_GSENSOR_RANGE_2G, .odr_hz = 100,
        .low_power = 0, .use_irq = 0,
    };
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);

    int self = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SELF_TEST, NULL);
    shell_print(sh, "self-test: %s (ret=%d)", (self == 0) ? "OK" : "FAIL", self);

    for (int i = 0; i < 5; i++)
    {
        posix_gsensor_axis_t a = {0};
        posix_ssize_t n = posix_read(gs, &a, sizeof(a));
        if (n < 0)
        {
            shell_error(sh, "read fail %d", (int)n);
            break;
        }
        shell_print(sh, "[poll %d] x=%d y=%d z=%d (mg)", i, a.x, a.y, a.z);
    }
    posix_close(gs);
    return 0;
}

static int do_gsensor_irq(const struct shell *sh)
{
    posix_fd_t gs = posix_open("/dev/gsensor0");
    if (gs == POSIX_FD_NULL) { shell_error(sh, "open /dev/gsensor0 failed"); return -1; }

    posix_gsensor_config_t cfg =
    {
        .range = POSIX_GSENSOR_RANGE_2G, .odr_hz = 100,
        .low_power = 0, .use_irq = 1,
    };
    int r = posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);
    if (r != 0)
    {
        shell_error(sh, "enable IRQ mode failed: %d (check int_pin_path)", r);
        posix_close(gs);
        return -1;
    }
    uint32_t tmo = 1000;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_TIMEOUT, &tmo);

    shell_print(sh, "IRQ mode: reading 5 samples (1s timeout each)...");
    for (int i = 0; i < 5; i++)
    {
        posix_gsensor_axis_t a = {0};
        posix_ssize_t n = posix_read(gs, &a, sizeof(a));
        if (n < 0)
        {
            shell_error(sh, "[irq %d] read fail %d (likely timeout)", i, (int)n);
            break;
        }
        shell_print(sh, "[irq %d] x=%d y=%d z=%d (mg)", i, a.x, a.y, a.z);
    }
    cfg.use_irq = 0;
    posix_ioctl(gs, POSIX_GSENSOR_IOCTL_SET_CONFIG, &cfg);
    posix_close(gs);
    return 0;
}

static int cmd_gsensor(const struct shell *sh, size_t argc, char **argv)
{
    if (!s_gs_inited) { posix_port_init_all(); s_gs_inited = true; }

    const char *mode = (argc >= 2) ? argv[1] : "poll";
    if (strcmp(mode, "irq") == 0)
    {
        return do_gsensor_irq(sh);
    }
    if (strcmp(mode, "poll") == 0)
    {
        return do_gsensor_poll(sh);
    }
    shell_error(sh, "usage: posix_gsensor [poll|irq]");
    return -1;
}

SHELL_CMD_REGISTER(posix_gsensor, NULL,
                   "POSIX gsensor test  (usage: posix_gsensor [poll|irq])",
                   cmd_gsensor);
#endif /* CONFIG_SHELL */

/* ================================================================
 * RTC 使用示例
 *
 * 功能：打开 /dev/rtc0 → 读能力 → 设时间 → 读回 → 装 5 秒后闹钟
 *       + 秒进位回调（打印每秒时间）
 *
 * 依赖：
 *   - DTS overlay 里 &rtc { status = "okay"; };
 *   - prj.conf 打开 CONFIG_RTC=y
 *   - 需要闹钟：CONFIG_RTC_ALARM=y
 *   - 需要每秒回调：CONFIG_RTC_UPDATE=y
 * ================================================================ */

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <string.h>
#include <stdlib.h>

/* 让上层能在回调里辨识是哪个 rtc；本 demo 只开一个，用一个静态指针传下去 */
struct rtc_ctx
{
    posix_fd_t fd;
};

static void on_alarm(posix_fd_t fd_unused, uint16_t id, void *arg)
{
    (void)fd_unused;
    struct rtc_ctx *ctx = (struct rtc_ctx *)arg;
    /* ISR / 工作队列上下文——不能阻塞。这里只做示意。 */
    posix_rtc_alarm_pending_t p = { .id = id };
    posix_ioctl(ctx->fd, POSIX_RTC_IOCTL_IS_ALARM_PENDING, &p);
    /* 业务侧一般 give 一个 semaphore 让任务去处理 */
}

static void on_second(posix_fd_t fd_unused, void *arg)
{
    (void)fd_unused;
    (void)arg;
    /* 每秒一次。中断上下文——不能阻塞。 */
}

void example_rtc(void)
{
    posix_fd_t rtc = posix_open("/dev/rtc0");
    if (rtc == POSIX_FD_NULL) { return; }

    /* === 1. 读能力 === */
    posix_rtc_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    posix_ioctl(rtc, POSIX_RTC_IOCTL_GET_CAPS, &caps);
    /* caps.alarm_count / caps.has_update_irq / caps.has_calibration */

    /* === 2. 设时间：2026-07-20 12:34:56 === */
    posix_rtc_time_t now =
    {
        .year   = 2026,
        .month  = 7,
        .mday   = 20,
        .hour   = 12,
        .minute = 34,
        .second = 56,
        .wday   = 0xFF,     /* 让驱动/软件自算 */
        .yday   = 0xFFFF,
        .nsec   = 0,
    };
    posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_TIME, &now);

    /* === 3. 读回时间 === */
    posix_rtc_time_t rd;
    posix_ioctl(rtc, POSIX_RTC_IOCTL_GET_TIME, &rd);
    /* rd.year=2026, rd.month=7, ... */

    /* === 4. 装闹钟：5 秒后触发（只匹配 second 字段） === */
    static struct rtc_ctx s_ctx;
    s_ctx.fd = rtc;

    if (caps.alarm_count > 0)
    {
        posix_rtc_alarm_t al =
        {
            .id       = 0,
            .mask     = POSIX_RTC_ALARM_MASK_SECOND,
            .time     = { .second = (uint8_t)((rd.second + 5) % 60) },
            .callback = on_alarm,
            .arg      = &s_ctx,
        };
        posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_ALARM, &al);
    }

    /* === 5. 秒进位回调 === */
    if (caps.has_update_irq)
    {
        posix_rtc_update_t upd = { .callback = on_second, .arg = &s_ctx };
        posix_ioctl(rtc, POSIX_RTC_IOCTL_SET_UPDATE_CB, &upd);
    }

    /* === 6. 关闭：会自动摘除闹钟/回调 ===
     * 实际使用中一般不 close，让 rtc 常驻。这里演示 API 完整用法。 */
    /* posix_close(rtc); */
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "posix_port.h"
LOG_MODULE_REGISTER(rtc_demo, LOG_LEVEL_DBG);

static bool         s_rtc_inited = false;
static posix_fd_t   s_rtc_fd     = POSIX_FD_NULL;
static struct rtc_ctx s_shell_ctx;
static volatile uint32_t s_tick_seconds;
static volatile uint32_t s_alarm_hits;

static void shell_on_alarm(posix_fd_t fd_unused, uint16_t id, void *arg)
{
    (void)fd_unused; (void)arg;
    posix_rtc_alarm_pending_t p = { .id = id };
    posix_ioctl(s_shell_ctx.fd, POSIX_RTC_IOCTL_IS_ALARM_PENDING, &p);
    s_alarm_hits++;
    LOG_INF("alarm id=%u hit (total=%u)", id, s_alarm_hits);
}

static void shell_on_second(posix_fd_t fd_unused, void *arg)
{
    (void)fd_unused; (void)arg;
    s_tick_seconds++;
    /* LOG_INF 在 deferred 模式下 ISR 安全，此处只在打印节流后触发 */
    if ((s_tick_seconds % 5) == 0)
    {
        LOG_INF("rtc second tick=%u", s_tick_seconds);
    }
}

static int open_once(const struct shell *sh)
{
    if (!s_rtc_inited) { posix_port_init_all(); s_rtc_inited = true; }
    if (s_rtc_fd != POSIX_FD_NULL) { return 0; }
    s_rtc_fd = posix_open("/dev/rtc0");
    if (s_rtc_fd == POSIX_FD_NULL)
    {
        shell_error(sh, "open /dev/rtc0 failed (check CONFIG_RTC=y and &rtc overlay)");
        return -1;
    }
    s_shell_ctx.fd = s_rtc_fd;
    return 0;
}

/* posix_rtc caps           — 打印能力
 * posix_rtc get            — 读当前时间
 * posix_rtc set YYYY MM DD hh mm ss   — 设时间
 * posix_rtc alarm SEC      — SEC 秒后触发一次闹钟
 * posix_rtc tick on/off    — 秒进位回调开/关
 * posix_rtc stat           — 打印统计（tick / alarm 次数）
 * posix_rtc close          — 关闭 fd（也顺带清所有回调）
 */
static int cmd_rtc(const struct shell *sh, size_t argc, char **argv)
{
    if (open_once(sh) != 0) { return -1; }

    if (argc < 2 || strcmp(argv[1], "caps") == 0)
    {
        posix_rtc_caps_t caps;
        int ret = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_CAPS, &caps);
        if (ret != POSIX_OK) { shell_error(sh, "GET_CAPS ret=%d", ret); return -1; }
        shell_print(sh,
                    "alarm_count=%u alarm_mask=0x%03x update_irq=%u calibration=%u",
                    caps.alarm_count, caps.alarm_mask,
                    caps.has_update_irq, caps.has_calibration);
        return 0;
    }

    if (strcmp(argv[1], "get") == 0)
    {
        posix_rtc_time_t t;
        int ret = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_TIME, &t);
        if (ret == POSIX_ERR_AGAIN)
        {
            shell_warn(sh, "time not set yet, use 'posix_rtc set ...' first");
            return 0;
        }
        if (ret != POSIX_OK) { shell_error(sh, "GET_TIME ret=%d", ret); return -1; }
        shell_print(sh, "%04u-%02u-%02u %02u:%02u:%02u",
                    t.year, t.month, t.mday, t.hour, t.minute, t.second);
        return 0;
    }

    if (strcmp(argv[1], "set") == 0)
    {
        if (argc < 8)
        {
            shell_print(sh, "usage: posix_rtc set YYYY MM DD hh mm ss");
            return 0;
        }
        posix_rtc_time_t t =
        {
            .year   = (uint16_t)strtoul(argv[2], NULL, 10),
            .month  = (uint8_t) strtoul(argv[3], NULL, 10),
            .mday   = (uint8_t) strtoul(argv[4], NULL, 10),
            .hour   = (uint8_t) strtoul(argv[5], NULL, 10),
            .minute = (uint8_t) strtoul(argv[6], NULL, 10),
            .second = (uint8_t) strtoul(argv[7], NULL, 10),
            .wday   = 0xFF,
            .yday   = 0xFFFF,
            .nsec   = 0,
        };
        int ret = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_SET_TIME, &t);
        if (ret != POSIX_OK) { shell_error(sh, "SET_TIME ret=%d", ret); return -1; }
        shell_print(sh, "ok");
        return 0;
    }

    if (strcmp(argv[1], "alarm") == 0)
    {
        int sec = (argc >= 3) ? (int)strtol(argv[2], NULL, 10) : 5;
        if (sec <= 0 || sec >= 60)
        {
            shell_error(sh, "sec must be 1..59 (only matches second field)");
            return -1;
        }

        posix_rtc_time_t now;
        int ret = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_GET_TIME, &now);
        if (ret != POSIX_OK) { shell_error(sh, "GET_TIME ret=%d", ret); return -1; }

        posix_rtc_alarm_t al =
        {
            .id       = 0,
            .mask     = POSIX_RTC_ALARM_MASK_SECOND,
            .time     = { .second = (uint8_t)((now.second + sec) % 60) },
            .callback = shell_on_alarm,
            .arg      = &s_shell_ctx,
        };
        ret = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_SET_ALARM, &al);
        if (ret != POSIX_OK) { shell_error(sh, "SET_ALARM ret=%d", ret); return -1; }
        shell_print(sh, "alarm armed, target second=%u", al.time.second);
        return 0;
    }

    if (strcmp(argv[1], "tick") == 0)
    {
        bool on = (argc >= 3) && (strcmp(argv[2], "on") == 0);
        posix_rtc_update_t upd = { .callback = on ? shell_on_second : NULL,
                                   .arg      = &s_shell_ctx
                                 };
        int ret = posix_ioctl(s_rtc_fd, POSIX_RTC_IOCTL_SET_UPDATE_CB, &upd);
        if (ret == POSIX_ERR_NOSUPP)
        {
            shell_warn(sh, "CONFIG_RTC_UPDATE not enabled");
            return 0;
        }
        if (ret != POSIX_OK) { shell_error(sh, "SET_UPDATE_CB ret=%d", ret); return -1; }
        shell_print(sh, "tick %s", on ? "on" : "off");
        return 0;
    }

    if (strcmp(argv[1], "stat") == 0)
    {
        shell_print(sh, "ticks=%u alarms=%u", s_tick_seconds, s_alarm_hits);
        return 0;
    }

    if (strcmp(argv[1], "close") == 0)
    {
        posix_close(s_rtc_fd);
        s_rtc_fd       = POSIX_FD_NULL;
        s_shell_ctx.fd = POSIX_FD_NULL;
        shell_print(sh, "closed");
        return 0;
    }

    shell_print(sh,
                "usage: posix_rtc [caps|get|set YYYY MM DD hh mm ss"
                "|alarm SEC|tick on|off|stat|close]");
    return 0;
}
SHELL_CMD_REGISTER(posix_rtc, NULL,
                   "posix_rtc [caps|get|set|alarm|tick|stat|close]",
                   cmd_rtc);
#endif /* CONFIG_SHELL */

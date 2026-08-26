/* ================================================================
 * RTC 驱动 — 基于 Zephyr RTC API 的 posix-device 适配层
 *
 * 路径格式: /dev/rtc<N>
 *   /dev/rtc0 → DTS 中 label 为 "rtc" 的片上 RTC 节点
 *
 * 依赖：
 *   1. DTS 中 &rtc { status = "okay"; }; （overlay 里打开）
 *   2. CONFIG_RTC=y
 *   3. 闹钟需 CONFIG_RTC_ALARM=y；秒回调需 CONFIG_RTC_UPDATE=y；
 *      校准需 CONFIG_RTC_CALIBRATION=y。缺失能力通过 GET_CAPS 上报。
 *
 * 时间表示转换：
 *   POSIX 层用 year=完整年 / month=1..12；
 *   Zephyr struct rtc_time 用 tm_year=year-1900 / tm_mon=0..11。
 *   仅在 <-> Zephyr 边界处做一次加减，业务层看到的都是自然值。
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <string.h>

/* ---------- 控制器私有数据 ---------- */
typedef struct
{
    const struct device *dev;
} rtc_drv_data_t;

/* ---------- per-open 文件私有数据 ---------- */
typedef struct rtc_file_s
{
    bool           in_use;
    rtc_drv_data_t *drv;

    /* 每个 fd 独立记录 alarm 回调，避免多 fd 相互覆盖；
     * 但 Zephyr 侧 alarm/update 是"每设备一份回调"，为简化：
     *   本端口对一个 rtc device 仅允许一份 file 注册回调，
     *   后开的 fd 想再注册回调会返回 POSIX_ERR_BUSY。
     *
     * 关于回调里的 posix_fd_t 形参：
     *   posix 框架不会把 fd 回递给驱动，因此本端口一律把 fd 参数
     *   置为 POSIX_FD_NULL。业务层需要区分多个 rtc 时，请通过 arg
     *   传递上下文（arg 里可放 fd、指针、通道号等）。 */
    struct
    {
        void (*cb)(posix_fd_t fd, uint16_t id, void *arg);
        void *arg;
    } alarm_cbs[8];   /* 上限 8 通道，足够覆盖 RTL87x3g 的 4 通道 */

    void (*update_cb)(posix_fd_t fd, void *arg);
    void  *update_arg;
} rtc_file_t;

#define MAX_RTC_FILES 2
static rtc_file_t s_rtc_files[MAX_RTC_FILES];

/* 反查表：Zephyr 回调只带 (dev, id)，需据此找回 rtc_file_t
 * —— 一个 device 最多只允许一个 fd 挂回调，因此一一映射就够。 */
static rtc_file_t *s_dev_owner[MAX_RTC_FILES];
static const struct device *s_dev_owner_key[MAX_RTC_FILES];

static rtc_file_t *find_owner(const struct device *dev)
{
    for (int i = 0; i < MAX_RTC_FILES; i++)
    {
        if (s_dev_owner_key[i] == dev)
        {
            return s_dev_owner[i];
        }
    }
    return NULL;
}

static int claim_owner(const struct device *dev, rtc_file_t *f)
{
    int free_slot = -1;
    for (int i = 0; i < MAX_RTC_FILES; i++)
    {
        if (s_dev_owner_key[i] == dev)
        {
            /* 已有 owner：若是自己则复用，否则冲突 */
            return (s_dev_owner[i] == f) ? POSIX_OK : POSIX_ERR_BUSY;
        }
        if (s_dev_owner_key[i] == NULL && free_slot < 0)
        {
            free_slot = i;
        }
    }
    if (free_slot < 0) { return POSIX_ERR_NOMEM; }
    s_dev_owner_key[free_slot] = dev;
    s_dev_owner[free_slot]     = f;
    return POSIX_OK;
}

static void release_owner(const struct device *dev, rtc_file_t *f)
{
    for (int i = 0; i < MAX_RTC_FILES; i++)
    {
        if (s_dev_owner_key[i] == dev && s_dev_owner[i] == f)
        {
            s_dev_owner_key[i] = NULL;
            s_dev_owner[i]     = NULL;
            return;
        }
    }
}

/* ---------- 时间格式转换 ---------- */
static void rtc_time_from_zephyr(const struct rtc_time *z, posix_rtc_time_t *p)
{
    p->year   = (uint16_t)(z->tm_year + 1900);
    p->month  = (uint8_t)(z->tm_mon + 1);
    p->mday   = (uint8_t)z->tm_mday;
    p->hour   = (uint8_t)z->tm_hour;
    p->minute = (uint8_t)z->tm_min;
    p->second = (uint8_t)z->tm_sec;
    p->wday   = (z->tm_wday < 0) ? 0xFF : (uint8_t)z->tm_wday;
    p->yday   = (z->tm_yday < 0) ? 0xFFFF : (uint16_t)z->tm_yday;
    p->nsec   = (uint32_t)z->tm_nsec;
}

static void rtc_time_to_zephyr(const posix_rtc_time_t *p, struct rtc_time *z)
{
    memset(z, 0, sizeof(*z));
    z->tm_year  = (int)p->year - 1900;
    z->tm_mon   = (int)p->month - 1;
    z->tm_mday  = (int)p->mday;
    z->tm_hour  = (int)p->hour;
    z->tm_min   = (int)p->minute;
    z->tm_sec   = (int)p->second;
    z->tm_wday  = (p->wday == 0xFF) ? -1 : (int)p->wday;
    z->tm_yday  = (p->yday == 0xFFFF) ? -1 : (int)p->yday;
    z->tm_isdst = -1;
    z->tm_nsec  = (int)p->nsec;
}

/* ---------- 编译时间兜底 ----------
 * 首次上电或掉电后 RTC 无有效数据时（rtc_get_time == -ENODATA），
 * 用 __DATE__/__TIME__ 作为默认时间。仅"比 2000-01-01 强"的兜底：
 *   - 烧录 → 上电之间的时间差没有校准
 *   - 增量编译时，只有本 .c 重编 __DATE__/__TIME__ 才会更新
 * 精确墙钟仍需上层用 NTP / 用户设定 SET_CALENDAR 覆盖。
 *
 * __DATE__ 格式："Jul 16 2026"（日 1-9 前有空格）
 * __TIME__ 格式："12:34:56"
 */
static int parse_month_abbr(const char *s)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (int i = 0; i < 12; i++)
    {
        if (s[0] == months[i * 3]     &&
            s[1] == months[i * 3 + 1] &&
            s[2] == months[i * 3 + 2])
        {
            return i + 1;   /* 返回 1..12 */
        }
    }
    return -1;
}

static void fill_build_time(struct rtc_time *z)
{
    const char *d = __DATE__;   /* "MMM DD YYYY"，DD 可能是 " D" */
    const char *t = __TIME__;   /* "HH:MM:SS" */

    memset(z, 0, sizeof(*z));
    z->tm_isdst = -1;
    z->tm_wday  = -1;
    z->tm_yday  = -1;

    int mon = parse_month_abbr(d);
    if (mon < 0) { mon = 1; }
    z->tm_mon  = mon - 1;

    /* 日：位置 4-5，可能是 " 5" 或 "15" */
    int day = 0;
    if (d[4] >= '0' && d[4] <= '9') { day = day * 10 + (d[4] - '0'); }
    if (d[5] >= '0' && d[5] <= '9') { day = day * 10 + (d[5] - '0'); }
    z->tm_mday = day > 0 ? day : 1;

    /* 年：位置 7-10 */
    int year = (d[7] - '0') * 1000 + (d[8] - '0') * 100
               + (d[9] - '0') * 10   + (d[10] - '0');
    z->tm_year = year - 1900;

    z->tm_hour = (t[0] - '0') * 10 + (t[1] - '0');
    z->tm_min  = (t[3] - '0') * 10 + (t[4] - '0');
    z->tm_sec  = (t[6] - '0') * 10 + (t[7] - '0');
}

/* ---------- Zephyr 回调 dispatcher ---------- */
#if defined(CONFIG_RTC_ALARM)
static void rtc_zephyr_alarm_cb(const struct device *dev, uint16_t id, void *user_data)
{
    (void)user_data;
    rtc_file_t *f = find_owner(dev);
    if (!f || id >= ARRAY_SIZE(f->alarm_cbs)) { return; }
    if (f->alarm_cbs[id].cb)
    {
        f->alarm_cbs[id].cb(POSIX_FD_NULL, id, f->alarm_cbs[id].arg);
    }
}
#endif

#if defined(CONFIG_RTC_UPDATE)
static void rtc_zephyr_update_cb(const struct device *dev, void *user_data)
{
    (void)user_data;
    rtc_file_t *f = find_owner(dev);
    if (!f || !f->update_cb) { return; }
    f->update_cb(POSIX_FD_NULL, f->update_arg);
}
#endif

/* ---------- open ---------- */
static void *rtc_open(void *drv_data, const char *path)
{
    (void)path;
    rtc_drv_data_t *d = (rtc_drv_data_t *)drv_data;
    if (!device_is_ready(d->dev)) { return POSIX_OPEN_ERR; }

    for (int i = 0; i < MAX_RTC_FILES; i++)
    {
        if (!s_rtc_files[i].in_use)
        {
            rtc_file_t *f = &s_rtc_files[i];
            memset(f, 0, sizeof(*f));
            f->in_use = true;
            f->drv    = d;
            return f;
        }
    }
    return POSIX_OPEN_ERR;
}

/* ---------- close ---------- */
static int rtc_close(void *drv_data, void *file_priv)
{
    (void)drv_data;
    rtc_file_t *f = (rtc_file_t *)file_priv;
    if (!f || !f->in_use) { return POSIX_OK; }

    /* 若此 fd 曾持有回调，卸掉 */
    if (find_owner(f->drv->dev) == f)
    {
#if defined(CONFIG_RTC_ALARM)
        for (size_t id = 0; id < ARRAY_SIZE(f->alarm_cbs); id++)
        {
            if (f->alarm_cbs[id].cb)
            {
                (void)rtc_alarm_set_callback(f->drv->dev, (uint16_t)id, NULL, NULL);
                (void)rtc_alarm_set_time(f->drv->dev, (uint16_t)id, 0, NULL);
            }
        }
#endif
#if defined(CONFIG_RTC_UPDATE)
        if (f->update_cb)
        {
            (void)rtc_update_set_callback(f->drv->dev, NULL, NULL);
        }
#endif
        release_owner(f->drv->dev, f);
    }

    f->in_use = false;
    return POSIX_OK;
}

/* ---------- read/write：RTC 不是流设备，一律拒绝 ---------- */
static posix_ssize_t rtc_read(void *drv_data, void *file_priv,
                              void *buf, size_t count)
{
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t rtc_write(void *drv_data, void *file_priv,
                               const void *buf, size_t count)
{
    (void)drv_data; (void)file_priv; (void)buf; (void)count;
    return POSIX_ERR_NOSUPP;
}

/* ---------- ioctl ---------- */
static int rtc_ioctl(void *drv_data, void *file_priv,
                     unsigned long cmd, void *arg)
{
    (void)drv_data;
    rtc_file_t *f = (rtc_file_t *)file_priv;
    const struct device *dev = f->drv->dev;

    switch (cmd)
    {
    case POSIX_RTC_IOCTL_GET_CALENDAR:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            struct rtc_time z;
            int ret = rtc_get_time(dev, &z);
            if (ret == -ENODATA) { return POSIX_ERR_AGAIN; }
            if (ret < 0)         { return POSIX_ERR_IO; }
            rtc_time_from_zephyr(&z, (posix_rtc_time_t *)arg);
            return POSIX_OK;
        }

    case POSIX_RTC_IOCTL_SET_CALENDAR:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            struct rtc_time z;
            rtc_time_to_zephyr((const posix_rtc_time_t *)arg, &z);
            int ret = rtc_set_time(dev, &z);
            if (ret == -EINVAL) { return POSIX_ERR_INVAL; }
            if (ret < 0)        { return POSIX_ERR_IO; }
            return POSIX_OK;
        }

    case POSIX_RTC_IOCTL_GET_CAPS:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_rtc_caps_t *caps = (posix_rtc_caps_t *)arg;
            memset(caps, 0, sizeof(*caps));

#if defined(CONFIG_RTC_ALARM)
            /* 探测：从 id=0 起问 supported_fields，遇 -EINVAL 说明超出范围 */
            uint16_t id = 0;
            uint16_t mask_union = 0;
            for (; id < 8; id++)
            {
                uint16_t m = 0;
                int ret = rtc_alarm_get_supported_fields(dev, id, &m);
                if (ret == -EINVAL || ret == -ENOSYS) { break; }
                if (ret < 0) { return POSIX_ERR_IO; }
                mask_union |= m;
            }
            caps->alarm_count = id;
            caps->alarm_mask  = mask_union;
#endif
#if defined(CONFIG_RTC_UPDATE)
            caps->has_update_irq = 1;
#endif
#if defined(CONFIG_RTC_CALIBRATION)
            caps->has_calibration = 1;
#endif
            return POSIX_OK;
        }

#if defined(CONFIG_RTC_ALARM)
    case POSIX_RTC_IOCTL_SET_ALARM:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_rtc_alarm_t *al = (posix_rtc_alarm_t *)arg;
            if (al->id >= ARRAY_SIZE(f->alarm_cbs)) { return POSIX_ERR_INVAL; }

            /* 独占持有：此 fd 独占该 device 的回调注册权 */
            int cr = claim_owner(dev, f);
            if (cr != POSIX_OK) { return cr; }

            /* 先注册回调（Zephyr 允许在 set_time 之前注册） */
            int ret = rtc_alarm_set_callback(dev,
                                             al->id,
                                             al->callback ? rtc_zephyr_alarm_cb : NULL,
                                             NULL);
            if (ret < 0 && ret != -ENOSYS)
            {
                return POSIX_ERR_IO;
            }
            f->alarm_cbs[al->id].cb  = al->callback;
            f->alarm_cbs[al->id].arg = al->arg;

            /* 再设时间；mask=0 相当于禁用 */
            struct rtc_time z;
            const struct rtc_time *zp = NULL;
            if (al->mask)
            {
                rtc_time_to_zephyr(&al->time, &z);
                zp = &z;
            }
            ret = rtc_alarm_set_time(dev, al->id, al->mask, zp);
            if (ret == -EINVAL) { return POSIX_ERR_INVAL; }
            if (ret == -ENOSYS) { return POSIX_ERR_NOSUPP; }
            if (ret < 0)        { return POSIX_ERR_IO; }

            /* mask=0 且没别的通道在用 → 释放独占 */
            if (al->mask == 0 && al->callback == NULL)
            {
                f->alarm_cbs[al->id].cb  = NULL;
                f->alarm_cbs[al->id].arg = NULL;
            }
            return POSIX_OK;
        }

    case POSIX_RTC_IOCTL_GET_ALARM:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_rtc_alarm_t *al = (posix_rtc_alarm_t *)arg;
            if (al->id >= ARRAY_SIZE(f->alarm_cbs)) { return POSIX_ERR_INVAL; }
            struct rtc_time z;
            uint16_t mask = 0;
            int ret = rtc_alarm_get_time(dev, al->id, &mask, &z);
            if (ret == -EINVAL) { return POSIX_ERR_INVAL; }
            if (ret == -ENOSYS) { return POSIX_ERR_NOSUPP; }
            if (ret < 0)        { return POSIX_ERR_IO; }
            al->mask = mask;
            rtc_time_from_zephyr(&z, &al->time);
            /* callback 字段不覆盖：来自本 fd 的注册值更权威 */
            return POSIX_OK;
        }

    case POSIX_RTC_IOCTL_IS_ALARM_PENDING:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_rtc_alarm_pending_t *p = (posix_rtc_alarm_pending_t *)arg;
            int ret = rtc_alarm_is_pending(dev, p->id);
            if (ret == -EINVAL) { return POSIX_ERR_INVAL; }
            if (ret == -ENOSYS) { return POSIX_ERR_NOSUPP; }
            if (ret < 0)        { return POSIX_ERR_IO; }
            p->pending = (uint8_t)(ret ? 1 : 0);
            return POSIX_OK;
        }
#else
    case POSIX_RTC_IOCTL_SET_ALARM:
    case POSIX_RTC_IOCTL_GET_ALARM:
    case POSIX_RTC_IOCTL_IS_ALARM_PENDING:
        return POSIX_ERR_NOSUPP;
#endif  /* CONFIG_RTC_ALARM */

#if defined(CONFIG_RTC_UPDATE)
    case POSIX_RTC_IOCTL_SET_UPDATE_CB:
        {
            if (posix_port_in_isr()) { return POSIX_ERR_ISR; }
            if (!arg) { return POSIX_ERR_INVAL; }
            posix_rtc_update_t *u = (posix_rtc_update_t *)arg;

            if (u->callback)
            {
                int cr = claim_owner(dev, f);
                if (cr != POSIX_OK) { return cr; }
            }

            int ret = rtc_update_set_callback(dev,
                                              u->callback ? rtc_zephyr_update_cb : NULL,
                                              NULL);
            if (ret == -ENOSYS) { return POSIX_ERR_NOSUPP; }
            if (ret < 0)        { return POSIX_ERR_IO; }
            f->update_cb  = u->callback;
            f->update_arg = u->arg;
            return POSIX_OK;
        }
#else
    case POSIX_RTC_IOCTL_SET_UPDATE_CB:
        return POSIX_ERR_NOSUPP;
#endif  /* CONFIG_RTC_UPDATE */

#if defined(CONFIG_RTC_CALIBRATION)
    case POSIX_RTC_IOCTL_SET_CALIBRATION:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int ret = rtc_set_calibration(dev, *(const int32_t *)arg);
            if (ret == -EINVAL) { return POSIX_ERR_INVAL; }
            if (ret == -ENOSYS) { return POSIX_ERR_NOSUPP; }
            if (ret < 0)        { return POSIX_ERR_IO; }
            return POSIX_OK;
        }
    case POSIX_RTC_IOCTL_GET_CALIBRATION:
        {
            if (!arg) { return POSIX_ERR_INVAL; }
            int ret = rtc_get_calibration(dev, (int32_t *)arg);
            if (ret == -ENOSYS) { return POSIX_ERR_NOSUPP; }
            if (ret < 0)        { return POSIX_ERR_IO; }
            return POSIX_OK;
        }
#else
    case POSIX_RTC_IOCTL_SET_CALIBRATION:
    case POSIX_RTC_IOCTL_GET_CALIBRATION:
        return POSIX_ERR_NOSUPP;
#endif  /* CONFIG_RTC_CALIBRATION */

    default:
        return POSIX_ERR_NOSUPP;
    }
}

/* ---------- 驱动函数表 ---------- */
static const posix_driver_ops_t g_rtc_ops =
{
    .open  = rtc_open,
    .close = rtc_close,
    .read  = rtc_read,
    .write = rtc_write,
    .ioctl = rtc_ioctl,
};

/* ---------- 设备实例 ----------
 * DTS 节点 label 见 zephyr/dts/arm/realtek/rtl87x3g.dtsi 的 "rtc: rtc@40000100"。
 * 该节点默认 status = "disabled"，应用侧需要在 board overlay 中开启：
 *   &rtc { status = "okay"; };
 * 并在 prj.conf 打开 CONFIG_RTC=y（可选 CONFIG_RTC_ALARM/_UPDATE/_CALIBRATION）。
 */
static rtc_drv_data_t s_rtc0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(rtc)) };

/* ---------- 自动注册 ---------- */
static int rtc_init(void)
{
    int ret = posix_device_register("/dev/rtc0", &g_rtc_ops, &s_rtc0);
    if (ret != POSIX_OK) { return ret; }

    /* 若 RTC 尚未有有效时间（首次上电 / 掉电无 backup），用编译时间兜底。
     * Zephyr rtl87x3g 驱动 get_time 从不返 -ENODATA，重启后从 1970-01-01
     * 计秒起走；因此改用"年份是否明显早于 __DATE__"来判定"没设过"。
     * 有电池/掉电保持的方案里 rtc 会返回真实时间，此路径不触发。 */
    if (device_is_ready(s_rtc0.dev))
    {
        struct rtc_time bt;
        fill_build_time(&bt);
        int bt_year = bt.tm_year + 1900;

        struct rtc_time cur;
        int gr = rtc_get_time(s_rtc0.dev, &cur);
        int cur_year = (gr == 0) ? (cur.tm_year + 1900) : 0;

        if (gr == -ENODATA || cur_year < bt_year)
        {
            (void)rtc_set_time(s_rtc0.dev, &bt);
        }
    }
    return POSIX_OK;
}
POSIX_INIT_DEVICE_EXPORT(rtc_init);

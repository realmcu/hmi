/* ================================================================
 * RTC driver — a posix-device adaptation layer on top of the Zephyr RTC API
 *
 * Path format: /dev/rtc<N>
 *   /dev/rtc0 → the on-chip RTC node labeled "rtc" in the DTS
 *
 * Dependencies:
 *   1. &rtc { status = "okay"; }; in the DTS (enabled in the overlay)
 *   2. CONFIG_RTC=y
 *   3. alarms need CONFIG_RTC_ALARM=y; the second callback needs CONFIG_RTC_UPDATE=y;
 *      calibration needs CONFIG_RTC_CALIBRATION=y. Missing capabilities are reported via GET_CAPS.
 *
 * Time representation conversion:
 *   the POSIX layer uses year=full year / month=1..12;
 *   Zephyr struct rtc_time uses tm_year=year-1900 / tm_mon=0..11.
 *   The adjustment is done only at the <-> Zephyr boundary, so the business layer always sees natural values.
 * ================================================================ */

#include "posix.h"
#include "posix_init.h"
#include "ioctls/posix_ioctl_rtc.h"

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <string.h>

/* ---------- Controller private data ---------- */
typedef struct
{
    const struct device *dev;
} rtc_drv_data_t;

/* ---------- per-open file private data ---------- */
typedef struct rtc_file_s
{
    bool           in_use;
    rtc_drv_data_t *drv;

    /* Each fd records alarm callbacks independently to avoid multiple fds overwriting each other;
     * but on the Zephyr side alarm/update is "one callback per device", so to simplify:
     *   this port allows only one file per rtc device to register callbacks,
     *   and a later fd trying to register will get POSIX_ERR_BUSY.
     *
     * About the posix_fd_t parameter in callbacks:
     *   the posix framework does not pass the fd back to the driver, so this port always sets
     *   the fd argument to POSIX_FD_NULL. When the business layer needs to distinguish multiple rtcs, pass
     *   context through arg (arg can hold the fd, a pointer, channel number, etc.). */
    struct
    {
        void (*cb)(posix_fd_t fd, uint16_t id, void *arg);
        void *arg;
    } alarm_cbs[8];   /* max 8 channels, enough to cover RTL87x3g's 4 channels */

    void (*update_cb)(posix_fd_t fd, void *arg);
    void  *update_arg;
} rtc_file_t;

/* app_time, FlashDB and diagnostics may each keep an RTC file open. */
#define MAX_RTC_FILES 4
static rtc_file_t s_rtc_files[MAX_RTC_FILES];

/* Reverse lookup table: Zephyr callbacks carry only (dev, id), so we must recover the rtc_file_t from them
 * -- a device allows at most one fd to have callbacks, so a one-to-one mapping suffices. */
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
            /* Existing owner: reuse if it is ourselves, otherwise conflict */
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

/* ---------- Time format conversion ---------- */
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

/* ---------- Build-time fallback ----------
 * When the RTC has no valid data on first power-on or after power loss (rtc_get_time == -ENODATA),
 * use __DATE__/__TIME__ as the default time. It is only a "better than 2000-01-01" fallback:
 *   - the time gap between flashing → power-on is not calibrated
 *   - on incremental builds, __DATE__/__TIME__ only updates when this .c is rebuilt
 * The precise wall clock still needs to be overridden by upper layers via NTP / user SET_CALENDAR.
 *
 * __DATE__ format: "Jul 16 2026" (days 1-9 have a leading space)
 * __TIME__ format: "12:34:56"
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
            return i + 1;   /* return 1..12 */
        }
    }
    return -1;
}

static void fill_build_time(struct rtc_time *z)
{
    const char *d = __DATE__;   /* "MMM DD YYYY", DD may be " D" */
    const char *t = __TIME__;   /* "HH:MM:SS" */

    memset(z, 0, sizeof(*z));
    z->tm_isdst = -1;
    z->tm_wday  = -1;
    z->tm_yday  = -1;

    int mon = parse_month_abbr(d);
    if (mon < 0) { mon = 1; }
    z->tm_mon  = mon - 1;

    /* Day: positions 4-5, may be " 5" or "15" */
    int day = 0;
    if (d[4] >= '0' && d[4] <= '9') { day = day * 10 + (d[4] - '0'); }
    if (d[5] >= '0' && d[5] <= '9') { day = day * 10 + (d[5] - '0'); }
    z->tm_mday = day > 0 ? day : 1;

    /* Year: positions 7-10 */
    int year = (d[7] - '0') * 1000 + (d[8] - '0') * 100
               + (d[9] - '0') * 10   + (d[10] - '0');
    z->tm_year = year - 1900;

    z->tm_hour = (t[0] - '0') * 10 + (t[1] - '0');
    z->tm_min  = (t[3] - '0') * 10 + (t[4] - '0');
    z->tm_sec  = (t[6] - '0') * 10 + (t[7] - '0');
}

/* ---------- Zephyr callback dispatcher ---------- */
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

    /* If this fd once held callbacks, remove them */
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

/* ---------- read/write: RTC is not a stream device, always reject ---------- */
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
            /* Probe: query supported_fields from id=0; -EINVAL means out of range */
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

            /* Exclusive hold: this fd exclusively owns the device's callback registration */
            int cr = claim_owner(dev, f);
            if (cr != POSIX_OK) { return cr; }

            /* Register the callback first (Zephyr allows registering before set_time) */
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

            /* Then set the time; mask=0 is equivalent to disabling */
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

            /* mask=0 and no other channel in use → release exclusive hold */
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
            /* Do not overwrite the callback field: the value registered by this fd is authoritative */
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

/* ---------- Driver ops table ---------- */
static const posix_driver_ops_t g_rtc_ops =
{
    .open  = rtc_open,
    .close = rtc_close,
    .read  = rtc_read,
    .write = rtc_write,
    .ioctl = rtc_ioctl,
};

/* ---------- Device instance ----------
 * The DTS node label is "rtc: rtc@40000100" in zephyr/dts/arm/realtek/rtl87x3g.dtsi.
 * That node defaults to status = "disabled"; the app must enable it in the board overlay:
 *   &rtc { status = "okay"; };
 * and enable CONFIG_RTC=y in prj.conf (optionally CONFIG_RTC_ALARM/_UPDATE/_CALIBRATION).
 */
static rtc_drv_data_t s_rtc0 = { .dev = DEVICE_DT_GET(DT_NODELABEL(rtc)) };

/* ---------- Auto-registration ---------- */
static int rtc_init(void)
{
    int ret = posix_device_register("/dev/rtc0", &g_rtc_ops, &s_rtc0);
    if (ret != POSIX_OK) { return ret; }

    /* If the RTC has no valid time yet (first power-on / power loss with no backup), fall back to the build time.
     * The Zephyr rtl87x3g driver's get_time never returns -ENODATA and counts seconds from 1970-01-01 after reset;
     * so instead we use "is the year clearly earlier than __DATE__" to decide "never set".
     * In designs with a battery / power-loss retention the rtc returns the real time, and this path does not trigger. */
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

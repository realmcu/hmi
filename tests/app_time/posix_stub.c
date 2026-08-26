/* tests/app_time/posix_stub.c */
#include "posix_stub.h"
#include <string.h>

posix_rtc_time_t stub_rtc;
void (*stub_tick_cb)(posix_fd_t fd, void *arg);
stub_event_t stub_events[STUB_MAX_EVENTS];
unsigned     stub_event_count;

int posix_port_init_all(void) { return 0; }
posix_fd_t posix_open(const char *p) { (void)p; return (posix_fd_t)1; }
int posix_close(posix_fd_t fd)       { (void)fd; return POSIX_OK; }

int posix_ioctl(posix_fd_t fd, unsigned long cmd, void *arg)
{
    (void)fd;
    switch (cmd)
    {
    case POSIX_RTC_IOCTL_GET_CALENDAR:
        *(posix_rtc_time_t *)arg = stub_rtc; return POSIX_OK;
    case POSIX_RTC_IOCTL_SET_CALENDAR:
        stub_rtc = *(posix_rtc_time_t *)arg; return POSIX_OK;
    case POSIX_RTC_IOCTL_SET_UPDATE_CB:
        stub_tick_cb = ((posix_rtc_update_t *)arg)->callback; return POSIX_OK;
    default: return POSIX_ERR_NOSUPP;
    }
}

int app_event_subscribe(app_event_id_t id, app_event_cb_t cb, void *u)
{ (void)id; (void)cb; (void)u; return 0; }
int app_event_publish(app_event_id_t id, const void *p, size_t l)
{ return app_event_publish_isr(id, p, l); }
int app_event_publish_isr(app_event_id_t id, const void *p, size_t l)
{
    if (stub_event_count >= STUB_MAX_EVENTS) { return -1; }
    stub_event_t *e = &stub_events[stub_event_count++];
    e->id = id; e->len = l;
    e->sec = (l == sizeof(uint32_t) && p) ? *(const uint32_t *)p : 0u;
    return 0;
}

void stub_events_reset(void)
{ memset(stub_events, 0, sizeof(stub_events)); stub_event_count = 0u; }
unsigned stub_event_count_of(app_event_id_t id)
{ unsigned n = 0; for (unsigned i = 0; i < stub_event_count; i++) if (stub_events[i].id == id)n++; return n; }
uint32_t stub_event_sec_of(app_event_id_t id)
{ for (unsigned i = 0; i < stub_event_count; i++) if (stub_events[i].id == id) return stub_events[i].sec; return 0u; }

void stub_set_rtc(uint16_t year, uint8_t mon, uint8_t mday,
                  uint8_t hour, uint8_t min, uint8_t sec)
{
    stub_rtc.year = year; stub_rtc.month = mon; stub_rtc.mday = mday;
    stub_rtc.hour = hour; stub_rtc.minute = min; stub_rtc.second = sec;
    stub_rtc.wday = 0xFFu; stub_rtc.yday = 0xFFFFu; stub_rtc.nsec = 0u;
}
void stub_tick(void)
{ if (stub_tick_cb) stub_tick_cb((posix_fd_t)1, NULL); }

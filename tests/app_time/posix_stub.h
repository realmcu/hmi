/* tests/app_time/posix_stub.h */
#ifndef POSIX_STUB_H
#define POSIX_STUB_H

#include "posix.h"
#include "ioctls/posix_ioctl_rtc.h"
#include "app_event.h"
#include <stdint.h>
#include <stddef.h>

extern posix_rtc_time_t stub_rtc;
extern void (*stub_tick_cb)(posix_fd_t fd, void *arg);

#define STUB_MAX_EVENTS 16u
typedef struct { app_event_id_t id; uint32_t sec; size_t len; } stub_event_t;
extern stub_event_t stub_events[STUB_MAX_EVENTS];
extern unsigned     stub_event_count;

void     stub_events_reset(void);
unsigned stub_event_count_of(app_event_id_t id);
uint32_t stub_event_sec_of(app_event_id_t id);
void     stub_set_rtc(uint16_t year, uint8_t mon, uint8_t mday,
                      uint8_t hour, uint8_t min, uint8_t sec);
void     stub_tick(void);

#endif

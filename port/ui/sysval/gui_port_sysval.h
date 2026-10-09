#ifndef GUI_PORT_SYSVAL_H
#define GUI_PORT_SYSVAL_H

#include <stdbool.h>
#include <stdint.h>

/* Product-specific values keep the BLE snapshot's epoch and 0.1 kcal units. */
#define WATCH_SYSVAL_KEY_EPOCH_SECONDS "watch/time/epoch_seconds"
#define WATCH_SYSVAL_KEY_CALORIES_DKCAL "watch/health/calories_dkcal"

void gui_port_sysval_time_subscribe(void);
void gui_port_sysval_power_update(uint8_t percent, bool charging);
void gui_port_sysval_ble_update(bool connected);

#endif

#include "gpio_api.h"
#include "gpio_irq_api.h"
#include "os_wrapper.h"

#include "dashboard_key.h"

#define NEXT_TRACK_BUTTON_PIN  PB_17
#define PREV_TRACK_BUTTON_PIN  PB_16
#define BACK_BUTTON_PIN  PB_14
#define POWER_BUTTON_PIN  PB_15

static key_send_hook_t g_key_hook = NULL;

gpio_irq_t next_track_button_irq;
gpio_irq_t dashboard_back_button_irq;
gpio_irq_t prev_track_button_irq;
gpio_irq_t dashboard_power_button_irq;

void key_set_send_hook(key_send_hook_t hook)
{
	g_key_hook = hook;
}

static void key_send(key_event_t event)
{
	/* Deliver to whichever BLE consumer (dashboard_ble / dashboard_bt_ext) has
	 * registered a hook. Runs in the producer context (button IRQ / CLI). */
	if (g_key_hook) {
		g_key_hook(event);
	}
}

void next_track_button_irq_handler(uint32_t id, uint32_t event)
{
	(void)id;
	(void)event;
	gpio_irq_disable(&next_track_button_irq);
	uint32_t level = GPIO_ReadDataBit(next_track_button_irq.pin);
	if (level == 0) {
		key_send(KEY_NEXT_TRACK_PRESS);
		gpio_irq_set(&next_track_button_irq, IRQ_RISE, 1);
	} else {
		key_send(KEY_NEXT_TRACK_RELEASE);
		gpio_irq_set(&next_track_button_irq, IRQ_FALL, 1);
	}
	gpio_irq_enable(&next_track_button_irq);
}

void dashboard_back_button_irq_handler(uint32_t id, uint32_t event)
{
	(void)id;
	(void)event;
	gpio_irq_disable(&dashboard_back_button_irq);
	uint32_t level = GPIO_ReadDataBit(dashboard_back_button_irq.pin);
	if (level == 0) {
		key_send(KEY_PLAY_PAUSE_PRESS);
		gpio_irq_set(&dashboard_back_button_irq, IRQ_RISE, 1);
	} else {
		key_send(KEY_PLAY_PAUSE_RELEASE);
		gpio_irq_set(&dashboard_back_button_irq, IRQ_FALL, 1);
	}
	gpio_irq_enable(&dashboard_back_button_irq);
}

void prev_track_button_irq_handler(uint32_t id, uint32_t event)
{
	(void)id;
	(void)event;
	gpio_irq_disable(&prev_track_button_irq);
	uint32_t level = GPIO_ReadDataBit(prev_track_button_irq.pin);
	if (level == 0) {
		key_send(KEY_PREV_TRACK_PRESS);
		gpio_irq_set(&prev_track_button_irq, IRQ_RISE, 1);
	} else {
		key_send(KEY_PREV_TRACK_RELEASE);
		gpio_irq_set(&prev_track_button_irq, IRQ_FALL, 1);
	}
	gpio_irq_enable(&prev_track_button_irq);
}

void dashboard_power_button_irq_handler(uint32_t id, uint32_t event)
{
	(void)id;
	(void)event;
	gpio_irq_disable(&dashboard_power_button_irq);
	uint32_t level = GPIO_ReadDataBit(dashboard_power_button_irq.pin);
	if (level == 0) {
		gpio_irq_set(&dashboard_power_button_irq, IRQ_RISE, 1);
	} else {
		gpio_irq_set(&dashboard_power_button_irq, IRQ_FALL, 1);
	}
	gpio_irq_enable(&dashboard_power_button_irq);
}

void dashboard_button_init(void)
{
    gpio_irq_init(&next_track_button_irq, NEXT_TRACK_BUTTON_PIN, next_track_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&next_track_button_irq)));
    gpio_irq_pull_ctrl(&next_track_button_irq, PullUp);
    gpio_irq_set(&next_track_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&next_track_button_irq);

    gpio_irq_init(&dashboard_back_button_irq, BACK_BUTTON_PIN, dashboard_back_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&dashboard_back_button_irq)));
    gpio_irq_pull_ctrl(&dashboard_back_button_irq, PullUp);
    gpio_irq_set(&dashboard_back_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&dashboard_back_button_irq);

    gpio_irq_init(&dashboard_power_button_irq, POWER_BUTTON_PIN, dashboard_power_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&dashboard_power_button_irq)));
    gpio_irq_pull_ctrl(&dashboard_power_button_irq, PullUp);
    gpio_irq_set(&dashboard_power_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&dashboard_power_button_irq);

    gpio_irq_init(&prev_track_button_irq, PREV_TRACK_BUTTON_PIN, prev_track_button_irq_handler, 
                  (uint32_t)((uintptr_t)(&prev_track_button_irq)));
    gpio_irq_pull_ctrl(&prev_track_button_irq, PullUp);
    gpio_irq_set(&prev_track_button_irq, IRQ_FALL, 1);
    gpio_irq_enable(&prev_track_button_irq);
}

u32 next_track_press(u16 argc, u8  *argv[])
{
	UNUSED(argc);
    UNUSED(argv);
	key_send(KEY_NEXT_TRACK_PRESS);
	return TRUE;
}

u32 next_track_release(u16 argc, u8  *argv[])
{
	UNUSED(argc);
    UNUSED(argv);
	key_send(KEY_NEXT_TRACK_RELEASE);
	return TRUE;
}

u32 prev_track_press(u16 argc, u8  *argv[])
{
	UNUSED(argc);
    UNUSED(argv);
	key_send(KEY_PREV_TRACK_PRESS);
	return TRUE;
}

u32 prev_track_release(u16 argc, u8  *argv[])
{
	UNUSED(argc);
    UNUSED(argv);
	key_send(KEY_PREV_TRACK_RELEASE);
	return TRUE;
}

u32 play_pause_press(u16 argc, u8  *argv[])
{
	UNUSED(argc);
    UNUSED(argv);
	key_send(KEY_PLAY_PAUSE_PRESS);
	return TRUE;
}

u32 play_pause_release(u16 argc, u8  *argv[])
{
	UNUSED(argc);
    UNUSED(argv);
	key_send(KEY_PLAY_PAUSE_RELEASE);
	return TRUE;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE next_track_press_cmd[] = {
    {"next_track_press", next_track_press},
};

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE next_track_release_cmd[] = {
    {"next_track_release", next_track_release},
};

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE prev_track_press_cmd[] = {
    {"prev_track_press", prev_track_press},
};

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE prev_track_release_cmd[] = {
    {"prev_track_release", prev_track_release},
};

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE play_pause_press_cmd[] = {
    {"play_pause_press", play_pause_press},
};

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE play_pause_release_cmd[] = {
    {"play_pause_release", play_pause_release},
};
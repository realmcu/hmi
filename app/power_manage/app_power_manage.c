#include "app_power_manage.h"

#include "app_dlps.h"
#include "ebadge_task.h"
#include "gui_api.h"
#include "gui_message.h"
#include "gui_port.h"
#include "gui_server.h"
#include "hmi_ble_gap_msg.h"
#include "section.h"
#include "trace.h"
#include "fmc_api.h"
#include "fmc_api_ext.h"
#if defined(CONFIG_RTK_BT_BREDR)
#include "hmi_br_edr_gap_init.h"
#endif
#if defined(CONFIG_SPI_AT_MASTER)
#include "spi_at_master.h"
#endif

#include <dlps_util.h>
#include <indirect_access.h>
#include <pm.h>
#include <power_manager_unit_platform.h>
#include <rtl876x_pinmux.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#define TOUCH_DEV_NODE          DT_NODELABEL(touch_device)
#define POWER_KEY_NODE          DT_NODELABEL(key1)
#define POWER_OFF_AON_REG       0x6U
#define POWER_OFF_AON_MAGIC     0xEB3AU

#if defined(CONFIG_RTL87X3G_USE_MIC1_P_AS_GPIOA25)
#define POWER_KEY_PAD           MIC1_P
#else
#define POWER_KEY_PAD           P6_0
#endif

static const struct gpio_dt_spec power_key = GPIO_DT_SPEC_GET(POWER_KEY_NODE, gpios);

/* P0_3/ADC_3 (GPIOA3) and P1_0 (GPIOA7) are the touch node's pwr-en/pwr pins,
 * so the Zephyr GPIO driver owns them -- see screen_on_off_power_set(). */
static const struct gpio_dt_spec touch_pwr_en = GPIO_DT_SPEC_GET(TOUCH_DEV_NODE, pwr_en_gpios);
static const struct gpio_dt_spec touch_pwr = GPIO_DT_SPEC_GET(TOUCH_DEV_NODE, pwr_gpios);
static const struct gpio_dt_spec touch_irq = GPIO_DT_SPEC_GET(TOUCH_DEV_NODE, irq_gpios);
static const struct gpio_dt_spec touch_rst = GPIO_DT_SPEC_GET(TOUCH_DEV_NODE, rst_gpios);

static bool power_off_requested = false;

static RAM_TEXT_SECTION void pad_all_power_off(void)
{
    /* P3_0 and P5_5 are declared as wakeup-low by the UART sleep pinctrl.
     * They are not product wake sources, so disable them before removing pad
     * power. GPIOA25/MIC1_P remains powered as the only key wake source. */
    System_WakeUpPinDisable(P3_0);
    System_WakeUpPinDisable(P5_0);
    System_WakeUpPinDisable(P5_5);

    /* Keep active-low/high enable pins driven to their definite OFF level. */
    Pad_Config(P2_6, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
               PAD_OUT_ENABLE, PAD_OUT_LOW);  /* external BT/RF path */

    // valid 15 -> 2 mA
    Pad_Config(P4_2, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
               PAD_OUT_ENABLE, PAD_OUT_LOW); /* WIFI QSPI */
    Pad_Config(P4_3, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
               PAD_OUT_ENABLE, PAD_OUT_LOW); /* WIFI QSPI */
    Pad_Config(P4_4, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
               PAD_OUT_ENABLE, PAD_OUT_LOW); /* WIFI QSPI */
    Pad_Config(P4_5, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
               PAD_OUT_ENABLE, PAD_OUT_LOW); /* WIFI QSPI */

    gpio_pin_set_dt(&touch_pwr_en, false);

    gpio_pin_set_dt(&touch_pwr, true);
    gpio_pin_set_dt(&touch_rst, false);
    gpio_pin_configure_dt(&touch_irq, GPIO_OUTPUT_LOW);
    gpio_pin_set_dt(&touch_irq, true);

    fmc_psram_enter_lpm(FMC_SPIC_ID_1, FMC_PSRAM_LPM_DEEP_POWER_DOWN_MODE);
    fmc_psram_enter_lpm(FMC_SPIC_ID_3, FMC_PSRAM_LPM_DEEP_POWER_DOWN_MODE);
}

void screen_on_off_power_set(bool on)
{
    /* These three pins are rail controls and must remain driven, not left
     * floating, while the controlled devices are off. */
    Pad_Config(ADC_2, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
               PAD_OUT_ENABLE, on ? PAD_OUT_HIGH : PAD_OUT_LOW);

    /* Fixed pins used by the selected ST7801N panel. The overlay only exposes
     * the display node, while the driver keeps these pin assignments locally. */
    if (!on)
    {
        Pad_Config(P2_2, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD TE */
        Pad_Config(P4_0, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD reset */
        Pad_Config(P9_0, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD QSPI D2 */
        Pad_Config(P9_1, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD QSPI D1 */
        Pad_Config(P9_2, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD QSPI CS */
        Pad_Config(P9_3, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD QSPI D0 */
        Pad_Config(P9_4, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD QSPI CLK */
        Pad_Config(P9_5, PAD_SW_MODE, PAD_SHUTDOWN, PAD_PULL_DOWN,
                   PAD_OUT_DISABLE, PAD_OUT_LOW); /* LCD QSPI D3 */
        Pad_Config(P2_5, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
                   PAD_OUT_ENABLE, PAD_OUT_LOW);  /* LCD backlight */
        Pad_Config(MICBIAS1, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN,
                   PAD_OUT_ENABLE, PAD_OUT_LOW);  /* motor rail */
    }
}

bool app_power_manage_is_power_off_requested(void)
{
    return power_off_requested;
}

void gui_port_reset_power_off_request(void)
{
    power_off_requested = false;
}

static RAM_TEXT_SECTION void power_key_wakeup_configure(void)
{
    /* MIC1_P is a codec hybrid pad. It must be switched to digital mode before
     * its pull resistor and AON input path can control GPIOA25. */
    Pad_AnalogMode(POWER_KEY_PAD, PAD_DIGITAL_MODE);
    /* In this SDK PAD_WEAKLY_PULL selects the lower 15 kOhm resistance while
     * PAD_STRONG_PULL selects 150 kOhm; use 15 kOhm so the line remains at the
     * 1.8 V idle level after all external rails are removed. */
    Pad_ConfigExt(POWER_KEY_PAD, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP,
                  PAD_OUT_DISABLE, PAD_OUT_LOW, PAD_WEAKLY_PULL);
    Pad_HighSpeedMuxSel(POWER_KEY_PAD, FROM_AON_DOMAIN);
    System_WakeUpDebounceCmd(POWER_KEY_PAD, PAD_WAKEUP_DISABLE);
    System_WakeUpDebounceDisable(POWER_KEY_PAD);
    Pad_ClearWakeupINTPendingBit(POWER_KEY_PAD);
    System_WakeUpPinEnable(POWER_KEY_PAD, PAD_WAKEUP_POL_LOW);
    System_WakeUpInterruptEnable(POWER_KEY_PAD);
}

static void power_key_wakeup_init(void)
{
    static bool store_cb_registered;

    power_key_wakeup_configure();

    /* pm_suspend_devices_rtk() reapplies GPIO state during POWER_STAGE_STORE.
     * Register after the platform callback so this configuration is the final
     * state written immediately before entering power-down. */
    if (!store_cb_registered)
    {
        int32_t ret = power_stage_cb_register(power_key_wakeup_configure, POWER_STAGE_STORE);
        if (ret == 0)
        {
            store_cb_registered = true;
        }
        else
        {
            DBG_DIRECT("[power-key] wake store callback register failed: %d\n", ret);
        }
    }
}

/*----------------------------------------------------------------------------*
 *  Power-off quiesce
 *
 *  power_mode_set(POWER_POWERDOWN_MODE) does not power anything down; it records
 *  the mode the SoC should take the next time the kernel reaches idle.  Idle is
 *  reached only when every thread is blocked, and it stays reached only while no
 *  kernel timeout is pending -- so a single running thread, or a single armed
 *  timer, is enough to keep the badge awake indefinitely.  Two of them exist by
 *  default and both fire forever: eb-tick reloads every 100 ms, and the deferred
 *  logging thread wakes every CONFIG_LOG_PROCESS_THREAD_SLEEP_MS (1000).
 *
 *  Hence this function, and hence its order: timers before threads.  Suspending a
 *  thread does nothing to a timeout it owns -- the kernel still wakes the CPU at
 *  the deadline, finds nobody to service it, and re-arms.
 *----------------------------------------------------------------------------*/
#define RADIO_IDLE_TIMEOUT_MS   500U
#define RADIO_IDLE_POLL_MS      10U
#define QUIESCE_MAX_THREADS     24U

static bool system_quiesced;

/* k_thread_foreach() runs its callback with the scheduler locked, and
 * k_thread_suspend() takes that same lock -- so the sweep is two passes: collect
 * under the lock (touching nothing but this array), suspend outside it. */
static struct k_thread *quiesce_list[QUIESCE_MAX_THREADS];
static uint8_t quiesce_n;

static void quiesce_collect(const struct k_thread *thread, void *user_data)
{
    ARG_UNUSED(user_data);

    if (quiesce_n < QUIESCE_MAX_THREADS)
    {
        quiesce_list[quiesce_n++] = (struct k_thread *)thread;
    }
}

/* Threads the shutdown runs on, or runs *through*.  Suspending these was the
 * cause of the endless
 *   os_wrapper_send_signal_to_task send fail: task_id 0 signal (type 106 ...)
 *   send_signal_generic fail: type:106, task_handle=0
 * after power-off: type 106 is the BT MAC handing a timer callback to a task to
 * run, and the two callbacks in the flood are the page-scan and inquiry-scan slot
 * timers -- which is why it repeated at exactly the 0x800-slot (1.28 s) scan
 * interval.  With the stack tasks frozen those signals had nowhere to land, and
 * the radio could never finish shutting down either.
 *
 * Keeping them costs nothing: every one blocks on a queue or semaphore with no
 * timeout, so none of them is a waker.  The wakers were eb-tick (stopped above)
 * and the logging thread's CONFIG_LOG_PROCESS_THREAD_SLEEP_MS sleep -- a plain
 * z_thread_timeout, which the platform's power-down timer sweep does not stop --
 * and that one is still suspended below. */
static const char *const quiesce_keep[] =
{
    "idle",           /* the PM hook runs *here*: this is how the SoC powers down  */
    "bt_task",        /* app IO pump and BT stack message pump in one              */
    "UpperStackTask", /* BLE/BR-EDR host                                           */
    "low_stack_task", /* BT controller -- owner of the type-106 signal queue       */
    "flash_task",     /* the stack still writes bond/config data on the way down   */
    "sysworkq",       /* os_timer_pend_function_call_zephyr() submits stack work   */
};

static bool quiesce_thread_is_kept(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(quiesce_keep); i++)
    {
        /* Prefix match, so "idle" also covers the per-CPU "idle 00" naming. */
        if (strncmp(name, quiesce_keep[i], strlen(quiesce_keep[i])) == 0)
        {
            return true;
        }
    }

    return false;
}

static void power_off_quiesce_threads(void)
{
    struct k_thread *self = k_current_get();

    quiesce_n = 0;
    k_thread_foreach(quiesce_collect, NULL);

    if (quiesce_n >= QUIESCE_MAX_THREADS)
    {
        /* Loud, because the overflowed tail is exactly the set of threads that
         * would then keep the badge awake with no clue as to why. */
        DBG_DIRECT("[power-key] thread list truncated at %u, some left RUNNING\n",
                   QUIESCE_MAX_THREADS);
    }

    for (uint8_t i = 0; i < quiesce_n; i++)
    {
        struct k_thread *thread = quiesce_list[i];
        const char *name = k_thread_name_get(thread);

        /* Us.  Everything below still has to run. */
        if (thread == self)
        {
            continue;
        }

        if (name != NULL && quiesce_thread_is_kept(name))
        {
            DBG_DIRECT("[power-key] keep thread %s\n", name);
            continue;
        }

        DBG_DIRECT("[power-key] suspend thread %s\n", name != NULL ? name : "<unnamed>");
        k_thread_suspend(thread);
    }
}

static void power_off_quiesce_system(void)
{
    if (system_quiesced)
    {
        return;
    }
    system_quiesced = true;

    /* Context matters here: both callers reach this on the GUI thread -- one from
     * a widget click callback, one via gui_send_msg_to_server() -- never from the
     * system workqueue. */

    /* 1. Timers and delayable work first, while every thread is still runnable. */
    gui_port_indev_quiesce();
    ebadge_task_quiesce();      /* eb-tick (100ms reload) + l2_task + port_tcp work */
#if defined(CONFIG_SPI_AT_MASTER)
    spi_at_master_quiesce();    /* S2M interrupt + ready timeout + at_client works  */
#endif

    /* 2. The radio, in the order the reference DLPS path uses: legacy BT out of
     *    scan first, then BLE advertising.  Both calls only *post* to the BT task,
     *    so issue both and then wait for both -- one settle instead of two. */
#if defined(CONFIG_RTK_BT_BREDR)
    bool bredr_pending = hmi_br_edr_request_radio_off();

    if (!bredr_pending)
    {
        DBG_DIRECT("[power-key] BR/EDR radio off request failed, scan stays up\n");
    }
#endif

    bool ble_pending = hmi_ble_request_adv_enable(false);

    if (!ble_pending)
    {
        DBG_DIRECT("[power-key] BLE adv off request failed, radio stays up\n");
    }

    if (ble_pending
#if defined(CONFIG_RTK_BT_BREDR)
        || bredr_pending
#endif
       )
    {
        uint32_t waited = 0;

        while (waited < RADIO_IDLE_TIMEOUT_MS)
        {
            bool ble_done = !ble_pending || hmi_ble_adv_is_idle();
#if defined(CONFIG_RTK_BT_BREDR)
            bool bredr_done = !bredr_pending || hmi_br_edr_radio_is_idle();
#else
            bool bredr_done = true;
#endif
            if (ble_done && bredr_done)
            {
                break;
            }

            k_msleep(RADIO_IDLE_POLL_MS);
            waited += RADIO_IDLE_POLL_MS;
        }

        DBG_DIRECT("[power-key] BLE adv %s (state %d) after %u ms\n",
                   hmi_ble_adv_is_idle() ? "idle" : "STILL UP",
                   hmi_ble_gap_get_adv_state(), waited);
#if defined(CONFIG_RTK_BT_BREDR)
        DBG_DIRECT("[power-key] BR/EDR scan %s\n",
                   hmi_br_edr_radio_is_idle() ? "idle" : "off, unconfirmed");
#endif
    }

    /* 3. Now the threads.  Past this point the logging thread is suspended, so
     *    only DBG_DIRECT() reaches the console. */
    power_off_quiesce_threads();

    /* 4. Release every wakelock. */
    app_dlps_enable(0xFFFFFFFFU);

    DBG_DIRECT("[power-key] quiesced: %u threads swept, timers stopped\n", quiesce_n);
}

void gui_port_power_down_enter(void)
{
    power_off_requested = true;
    screen_on_off_power_set(false);
    pad_all_power_off();
    power_key_wakeup_init();
    bt_power_mode_set(BTPOWER_DEEP_SLEEP);

    int32_t ret = power_mode_set(POWER_POWERDOWN_MODE);
    if (ret != 0)
    {
        DBG_DIRECT("[power-key] set power-down failed: %d\n", ret);
    }
}

static void power_off(void)
{
    if (power_off_requested)
    {
        return;
    }
    power_off_requested = true;

    DBG_DIRECT("[power-key] power off requested\n");

    /* Panel off first, so the badge looks off immediately. */
    gui_dispdev_t *dc = gui_get_dc();
    dc->lcd_power_off();

    extern void gui_set_keep_active_time(uint32_t active_time);
    gui_set_keep_active_time(0);

    power_off_quiesce_system();

    /* The marker survives power-down and distinguishes a product power-off from
     * a normal cold boot. The PM store callback puts PSRAM into deep power-down. */
    btaon_fast_write_safe(POWER_OFF_AON_REG, POWER_OFF_AON_MAGIC);
    gui_port_power_down_enter();
}

void gui_port_power_off(void)
{
    power_off();
}

#if defined(CONFIG_SHELL)
static void shell_command_power_off(void)
{
    power_off();
}

static int cmd_power_off(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    gui_msg_t msg = {.event = GUI_EVENT_USER_DEFINE, .cb = (gui_msg_cb)shell_command_power_off};
    gui_send_msg_to_server(&msg);

    return 0;
}

SHELL_CMD_REGISTER(power_off, NULL, "Power off the device", cmd_power_off);
#endif /* CONFIG_SHELL */

void app_power_manage_handle_long_press(void)
{
    if (power_off_requested)
    {
        /* Power-down wake is handled by gui_port_power_on_gate() during boot.
         * This branch only applies to a screen-off state that did not power down. */
        DBG_DIRECT("[power-key] wake long press reached %u ms\n", APP_POWER_LONG_PRESS_MS);
        btaon_fast_write_safe(POWER_OFF_AON_REG, 0);
        sys_reboot(SYS_REBOOT_COLD);
    }
    else
    {
        DBG_DIRECT("[power-key] power-off long press reached %u ms\n", APP_POWER_LONG_PRESS_MS);
        extern void ui_confirm_switchoff(uint32_t payload);
        ui_confirm_switchoff(0);
    }
}

bool gui_port_power_on_gate(void)
{
    uint16_t marker = btaon_fast_read_safe(POWER_OFF_AON_REG);

    if (marker != POWER_OFF_AON_MAGIC)
    {
        return true;
    }

    uint8_t reason = power_down_check_wake_up_reason();
    int key_level = gpio_is_ready_dt(&power_key) ? gpio_pin_get_dt(&power_key) : -1;

    DBG_DIRECT("[power-key] boot marker=0x%04x pd_reason=0x%02x key=%d\n",
               marker, reason, key_level);

    if ((reason & POWER_DOWN_WAKEUP_PAD) == 0U)
    {
        DBG_DIRECT("[power-key] non-PAD wake rejected\n");
        return false;
    }

    if (key_level <= 0)
    {
        DBG_DIRECT("[power-key] wake press released before gate\n");
        return false;
    }

    uint32_t start_ms = k_uptime_get_32();
    while (gpio_pin_get_dt(&power_key) > 0)
    {
        uint32_t elapsed = k_uptime_get_32() - start_ms;
        if (elapsed >= APP_POWER_LONG_PRESS_MS)
        {
            btaon_fast_write_safe(POWER_OFF_AON_REG, 0);
            DBG_DIRECT("[power-key] power on accepted after %u ms\n", elapsed);
            return true;
        }
        k_sleep(K_MSEC(10));
    }

    DBG_DIRECT("[power-key] power on rejected: short press\n");
    return false;
}

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/devicetree.h>
#include "trace.h"
#include "hmi_bt_task.h"
#include "ebadge_task.h"      /* V1.2 protocol stack entry point            */
#include "port/ebadge_port_antenna.h"  /* the shared antenna -- parked below  */
#include "gui_server.h"
#include "app_power_manage.h"
#include "rtl876x_pinmux.h"
#include "app_lower_init.h"
#include "app_dlps.h"
#include <pm.h>

/* AT-over-SPI toward the 8711, with US as the master.  Only the transport is
 * started here -- everything above it (at_client, at_wifi, at_socket) is brought
 * up by ebadge_task_init() through the port layer, because that is the layer that
 * knows when a radio is wanted. */
#if defined(CONFIG_SPI_AT_MASTER)
#include "spi_at_master.h"
#endif


#define APP_EBADGE_VERSION "0.0.4"

int main(void)
{
    /* DLPS must be initialized and locked before any other initialization. */
    app_dlps_init();
    app_dlps_disable(APP_DLPS_ENTER_CHECK_INIT);

    /* A PAD exits power-down by rebooting the SoC. Before powering any
     * application-controlled rail, require the wake key to remain pressed for
     * three seconds. A short press releases the init lock and returns to
     * power-down without starting the application. */
    if (!gui_port_power_on_gate())
    {
        app_dlps_enable(APP_DLPS_ENTER_CHECK_INIT);
        gui_port_power_down_enter();
        return 0;
    }

#ifndef CONFIG_UART_CONSOLE
    DBG_DIRECT("!!!!! remap log pin!!!");
    Pinmux_Config(P2_0, IDLE_MODE);
    Pinmux_Config(P3_1, UART1_TX);
    Pad_Config(P3_1, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_LOW);
    DBG_DIRECT("!!!!! remap log pin!!!");
#endif

    // io init
    // WiFi VD33_WF_EN P0_2
    Pad_Config(ADC_2, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_HIGH);

    /* Antenna to BLE before anything can transmit.  P2_6 is an RF switch, not an
     * enable: HIGH wires the shared antenna to our BLE radio, LOW to the 8711's
     * Wi-Fi.  It must be parked here, ahead of hmi_bt_task_init() below, because a
     * badge that starts advertising with the switch on the 8711 is invisible while
     * every layer reports healthy.
     *
     * Through the module rather than a raw Pad_Config, which is what this was: the
     * two Wi-Fi sessions borrow the antenna and hand it back, and that bookkeeping
     * only holds if one place owns the pin.  Idempotent, so the second call from
     * ebadge_task_init() costs a register write. */
    ebadge_port_antenna_init();


    app_system_lower_init();
    printf("\nHello World! Version: %s, %s\n", APP_EBADGE_VERSION, CONFIG_BOARD_TARGET);
    printf("[main] thread id: %p, name: %s, priority: %d\n",
           k_current_get(),
           k_thread_name_get(k_current_get()),
           k_thread_priority_get(k_current_get()));

    hmi_bt_task_init();

#if defined(CONFIG_SPI_AT_MASTER)
    /* AT-over-SPI toward the 8711 with the roles reversed: we drive SCLK and CS
     * at 25 MHz on spi1_hs, and the two sync lines swap direction (P4_1 becomes
     * our input, P2_7 our output).
     *
     * PLACEMENT, both ends.  It must come after app_system_lower_init(), because
     * the 16 KiB slot buffers live in SPIC1 PSRAM and that window only exists
     * once SPIC1 is up.  It must come before ebadge_task_init(), because
     * port_softap's init reaches down through at_wifi and at_client to this same
     * transport -- init is idempotent, so calling it there would work either way,
     * but then a transport failure would be reported from inside the protocol
     * stack instead of here where the reason is legible.
     *
     * ADC_2 (VD33_WF_EN) went high at the top of main(), so the 8711 has had the
     * whole lower-init sequence to boot.  If it got there first, S2M is already
     * high and its rising edge is gone; spi_at_master_init() detects that and
     * synthesises the go-ahead rather than waiting forever for an edge that has
     * passed.
     *
     * Non-fatal: BLE and the GUI do not need this link.  Every Wi-Fi entry point
     * above it degrades to a refusal, so the badge answers NOT_READY to a
     * transfer offer rather than failing to boot. */
    {
        int atm_rc = spi_at_master_init();
        if (atm_rc != 0)
        {
            printf("[main] spi_at_master_init failed %d -- link down, "
                   "`atm info` for why\n", atm_rc);
        }
    }
#endif

    /* V1.2 protocol stack -- one call spins up:
     *   - l2_task (msg queue + tick timer)
     *   - port_ble (binds hmi_ctrl_service RX/TX/CCCD)
     *   - port_softap (at_wifi; puts nothing on the wire until a phone connects)
     *   - xfer_session (registers tick sink)
     *   - all 0x01..0x1A command handlers
     * See app/protocol/ebadge_task.c for the wiring order. */
    ebadge_task_init();

    gui_server_init();
    gui_set_keep_active_time(0xFFFFFFFF);

    app_dlps_enable(APP_DLPS_ENTER_CHECK_INIT);
    return 0;
}

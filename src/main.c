/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("RTL8773G HMI Application Started");
    LOG_INF("Board: %s", CONFIG_BOARD);

    while (1) {
        LOG_INF("Hello from HMI application");
        k_sleep(K_SECONDS(5));
    }

    return 0;
}

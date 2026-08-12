/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <zephyr/kernel.h>
#include <os_msg.h>
#include <app_ble_gap.h>

#define BLE_TASK_PRIORITY           5
#define BLE_TASK_STACK_SIZE         4096
#define MAX_NUMBER_OF_GAP_MESSAGE   0x20
#define MAX_NUMBER_OF_IO_MESSAGE    0x20
#define MAX_NUMBER_OF_EVENT_MESSAGE (MAX_NUMBER_OF_GAP_MESSAGE + MAX_NUMBER_OF_IO_MESSAGE)

static void *io_queue_handle;
static void *evt_queue_handle;

K_THREAD_STACK_DEFINE(ble_task_stack, BLE_TASK_STACK_SIZE);
static struct k_thread ble_thread_data;

static void ble_thread_func(void *p1, void *p2, void *p3)
{
    uint8_t event;

    /* The BLE stack and application share event IDs; application payloads use a separate queue. */
    os_msg_queue_create(&io_queue_handle, "ioQ", MAX_NUMBER_OF_IO_MESSAGE, sizeof(T_IO_MSG));
    os_msg_queue_create(&evt_queue_handle, "evtQ", MAX_NUMBER_OF_EVENT_MESSAGE, sizeof(uint8_t));

    gap_start_bt_stack(evt_queue_handle, io_queue_handle, MAX_NUMBER_OF_GAP_MESSAGE);

    while (1)
    {
        if (os_msg_recv(evt_queue_handle, &event, 0xFFFFFFFF) == true)
        {
            if (event == EVENT_IO_TO_APP)
            {
                T_IO_MSG io_msg;
                if (os_msg_recv(io_queue_handle, &io_msg, 0) == true)
                {
                    app_ble_gap_handle_io_msg(io_msg);
                }
            }
            else
            {
                gap_handle_msg(event);
            }
        }
    }
}

void ble_task_init(void)
{
    k_thread_create(&ble_thread_data,
                    ble_task_stack,
                    K_THREAD_STACK_SIZEOF(ble_task_stack),
                    ble_thread_func,
                    NULL,
                    NULL,
                    NULL,
                    BLE_TASK_PRIORITY,
                    0,
                    K_NO_WAIT);
}

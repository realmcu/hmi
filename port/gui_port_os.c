/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gui_port.h"
#include "gui_api.h"
#include "guidef.h"
#include "ameba_soc.h"
#include "os_wrapper.h"
#include <stdarg.h>

// FreeRTOS wrapper functions for HoneyGUI OS abstraction layer

static void *port_thread_create(const char *name, void (*entry)(void *param), void *param,
                                uint32_t stack_size, uint8_t priority)
{
    rtos_task_t task_handle = NULL;
    (void)priority;
    if (rtos_task_create(&task_handle, name, entry, param, stack_size, 3) != RTK_SUCCESS) {
        return NULL;
    }

    return (void *)task_handle;
}

static bool port_thread_delete(void *handle)
{
    if (handle == NULL) {
        return false;
    }

    rtos_task_delete((rtos_task_t)handle);
    return true;
}

static bool port_thread_mdelay(uint32_t ms)
{
    rtos_time_delay_ms(ms);
    return true;
}

static uint32_t port_thread_ms_get(void)
{
    return rtos_time_get_current_system_time_ms();
}

static bool port_mq_create(void *handle, const char *name, uint32_t msg_size, uint32_t max_msgs)
{
    rtos_queue_t queue_handle = NULL;
    rtos_queue_t *queue_ptr = (rtos_queue_t *)handle;

    (void)name; // Name is not used in FreeRTOS queue creation

    if (rtos_queue_create(&queue_handle, max_msgs, msg_size) != RTK_SUCCESS) {
        return false;
    }

    *queue_ptr = queue_handle;
    return true;
}

static bool port_mq_send(void *handle, void *buffer, uint32_t size, uint32_t timeout)
{
    (void)size; // Size is determined at queue creation time

    if (handle == NULL || buffer == NULL) {
        return false;
    }

    return rtos_queue_send((rtos_queue_t)handle, buffer, timeout) == RTK_SUCCESS;
}

static bool port_mq_recv(void *handle, void *buffer, uint32_t size, uint32_t timeout)
{
    (void)size; // Size is determined at queue creation time

    if (handle == NULL || buffer == NULL) {
        return false;
    }

    return rtos_queue_receive((rtos_queue_t)handle, buffer, timeout) == RTK_SUCCESS;
}

static void *port_malloc(uint32_t n)
{
    return rtos_mem_malloc(n);
}

static void *port_realloc(void *ptr, uint32_t n)
{
    return rtos_mem_realloc(ptr, n);
}

static void port_free(void *rmem)
{
    rtos_mem_free(rmem);
}

static void port_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    // Use Ameba's logging system
    vprintf(format, args);

    va_end(args);
}

// Memory heap configuration
// Note: Reduce these values if you encounter RAM overflow during linking
#define PORT_GUI_MEMHEAP_SIZE (1024 * 192)  // 128KB for GUI memory
#define PORT_GUI_LOWER_MEMHEAP_SIZE (1024 * 1024 * 4)  // 4MB for lower priority GUI memory

static uint8_t gui_memheap[PORT_GUI_MEMHEAP_SIZE] __attribute__((aligned(32)));
//static uint8_t gui_lower_memheap[PORT_GUI_LOWER_MEMHEAP_SIZE] __attribute__((aligned(32)));

static struct gui_os_api os_api =
{
    .name = "freertos_ameba",
    .thread_create = port_thread_create,
    .thread_delete = port_thread_delete,
    .thread_mdelay = port_thread_mdelay,
    .thread_ms_get = port_thread_ms_get,
    .mq_create = port_mq_create,
    .mq_send = port_mq_send,
    .mq_recv = port_mq_recv,
    .f_malloc = port_malloc,
    .f_realloc = port_realloc,
    .f_free = port_free,

    .mem_addr = gui_memheap,
    .mem_size = PORT_GUI_MEMHEAP_SIZE,

    // .lower_mem_addr = gui_lower_memheap,
    // .lower_mem_size = PORT_GUI_LOWER_MEMHEAP_SIZE,
    .mem_threshold_size = 50 * 1024,  // 10KB threshold

    .log = (void *)port_log,
};

// GUI timer callback - called every 10ms
static void gui_timer_callback(void *arg)
{
    (void)arg;

    if (os_api.gui_tick_hook != NULL) {
        os_api.gui_tick_hook();
    }
}

void gui_port_os_init(void)
{
    // Create a periodic timer for GUI tick updates (10ms period)
    rtos_timer_t timer_handle = NULL;
    if (rtos_timer_create(&timer_handle, "gui_timer", NULL, 10,
                          TRUE, gui_timer_callback) == RTK_SUCCESS) {
        rtos_timer_start(timer_handle, 0);
    }
    os_api.lower_mem_addr = malloc(PORT_GUI_LOWER_MEMHEAP_SIZE);
    os_api.lower_mem_size = PORT_GUI_LOWER_MEMHEAP_SIZE;

    printf("lower_mem_addr: %p, lower_mem_size: %lu\n", os_api.lower_mem_addr, (unsigned long)os_api.lower_mem_size);
    // Register OS API with HoneyGUI
    gui_os_api_register(&os_api);
}

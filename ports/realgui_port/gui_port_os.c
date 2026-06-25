/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "guidef.h"
#include "gui_port.h"
#include "os_timer.h"
#include <os_msg.h>
#include <os_task.h>
#include <os_sync.h>
#include "platform_utils.h"
#include "trace.h"
#include "stdarg.h"
#include "os_sched.h"
#include "string.h"
#include "trace.h"
#include "wdg.h"
#include "gui_server.h"

extern bool gui_port_dc_lcd_is_work(void);

static bool ble_bond_new_device_flag = false;

void app_gui_set_bond_flag(void)
{
    APP_PRINT_INFO0("app_gui_set_bond_flag");
//    if (gui_port_dc_lcd_is_work())
//    {
//        ble_bond_new_device_flag = true;
//    }
}

typedef struct NEW_BLOCK_LINK
{
    union
    {
        struct NEW_BLOCK_LINK *pxNextFreeBlock;   /*<< The next free block in the list. */
        size_t LRValue;
    };
    size_t xBlockSize : 20;                     /*<< The size of the free block. */
    size_t magic : 8;
    size_t xRamType : 3;
    size_t xAllocateBit : 1;
//    size_t LRValue;
//    size_t allocTime;
} NewBlockLink_t;
#define FreeRTOS_portBYTE_ALIGNMENT     8

void *port_thread_create(const char *name, void (*entry)(void *param), void *param,
                         uint32_t stack_size, uint8_t priority)
{
    void *handle = NULL;
    if (os_task_create(&handle, name, entry, 0, stack_size, 1))
    {
        return handle;
    }
    else
    {
        return NULL;
    }
}
bool port_thread_delete(void *handle)
{
    return os_task_delete(handle);
}

bool port_thread_suspend(void *handle)
{
    return os_task_suspend(handle);
}

bool port_thread_resume(void *handle)
{
    return os_task_resume(handle);
}

bool port_thread_mdelay(uint32_t ms)
{
    // platform_delay_ms(ms);
    os_delay(ms);  //if open, when tab was slid, IDLE stack will overflow
    return true;
}

uint32_t port_thread_ms_get(void)
{
    static uint32_t time_ms_record = 0;
    static uint32_t time_ms_last = 0;
    uint32_t time_ms = sys_timestamp_get(); // max: 0xFFFFFFFF / 1000 ms
    if (time_ms_last > time_ms)
    {
        time_ms_record += 0xFFFFFFFF / 1000; // overflow
    }
    time_ms_last = time_ms;
    return time_ms_record + time_ms;
}

uint32_t port_thread_us_get(void)
{
    /*sys_timestamp_get_us() and sys_timestamp_get() should be called carefully*/
    /*overflow will occur after 1.193 hour*/
    return sys_timestamp_get();
}

bool port_thread_ctx_malloc(uint32_t size)
{
    return true;
}




#include "stdlib.h"
void *port_malloc(uint32_t n)
{
    return malloc(n);
}


void port_free(void *rmem)
{
    free(rmem);
}

void *port_realloc(void *ptr, uint32_t n)
{
    if (ptr == NULL)
    {
        return malloc(n);  // Allocate new memory if the original pointer is NULL
    }

    if (n == 0)    // If the new size is zero, free the memory and return NULL
    {
        free(ptr);
        return NULL;
    }

    NewBlockLink_t *pxLink = (NewBlockLink_t *)((uint8_t *)ptr - sizeof(NewBlockLink_t));
    size_t old_size = pxLink->xBlockSize;

    // Check if the current block can satisfy the new size without additional allocation
    if ((old_size - sizeof(NewBlockLink_t)) == (n + (FreeRTOS_portBYTE_ALIGNMENT - (n &
                                                                                    (FreeRTOS_portBYTE_ALIGNMENT - 1)))))
    {
        return ptr;  // No need to reallocate if the size is effectively unchanged
    }

    os_sched_suspend();

    void *new_ptr = malloc(n);  // Allocate new memory
    if (new_ptr == NULL)    // Check if malloc failed
    {
        os_sched_resume();
        return NULL;  // Allocation failed, returning NULL
    }

    // Copy the memory if the allocation was successful
    size_t min_size = (old_size - sizeof(NewBlockLink_t)) < n ? (old_size - sizeof(NewBlockLink_t)) : n;
    memcpy(new_ptr, ptr, min_size);

    free(ptr);  // Free the original memory

    os_sched_resume();
    return new_ptr;
}
#include "gui_api.h"


#include "trace.h"

static void port_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[256];
    vsnprintf(buf, sizeof(buf), format, args);

    //APP_PRINT_INFO1("[GUI MODULE]%s", TRACE_STRING(buf));
    DBG_DIRECT("[GUI MODULE]%s", buf);

    va_end(args);
}

static bool port_mq_create(void *handle, const char *name, uint32_t msg_size, uint32_t max_msgs)
{
    return os_msg_queue_create(handle, name, max_msgs, msg_size);
}

static bool port_mq_send(void *handle, void *buffer, uint32_t size, uint32_t timeout)
{
    return os_msg_send(handle, buffer, timeout);
}

static bool port_mq_recv(void *handle, void *buffer, uint32_t size, uint32_t timeout)
{
    return os_msg_recv(handle, buffer, timeout);
}

#define GUI_HEAP_SIZE                                           (50 * 1024)

static uint8_t port_mem_heap[GUI_HEAP_SIZE] = {0};

static struct gui_os_api os_api =
{
    .name = "rtk_osif",
    .thread_create = port_thread_create,
    .thread_delete = port_thread_delete,
    .thread_suspend = port_thread_suspend,
    .thread_resume = port_thread_resume,
    .thread_mdelay = port_thread_mdelay,
    .thread_ms_get = port_thread_ms_get,
    .thread_us_get = port_thread_us_get,
    .mq_create = port_mq_create,
    .mq_send = port_mq_send,
    .mq_recv = port_mq_recv,
    .f_malloc = port_malloc,
    .f_free = port_free,
    .f_realloc = port_realloc,
    .mem_addr = (void *)port_mem_heap,
    .mem_size = GUI_HEAP_SIZE,

    .lower_mem_addr = (void *)(0x4000000 + 360 * 360 * 2 * 2),
    .lower_mem_size = 0x200000,
    .mem_threshold_size = 10 * 1024,

#if (F_APP_GUI_USE_PSRAM == 1)
    .lower_mem_addr = (void *)PSRAM_GUI_HEAP_ADDR,
    .lower_mem_size = PSRAM_GUI_HEAP_SIZE,
#endif

    .log = port_log,
};

void gui_port_os_add_exe_to_gui_task()
{
    //APP_PRINT_INFO0("feed watchdog");
    wdg_kick();

    if (ble_bond_new_device_flag)
    {
        ble_bond_new_device_flag = false;
        os_delay(1000);
    }
}

void gui_port_os_init(void)
{
    gui_os_api_register(&os_api);
    gui_task_ext_execution_sethook(gui_port_os_add_exe_to_gui_task);
}


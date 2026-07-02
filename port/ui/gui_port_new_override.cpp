/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * ---------------------------------------------------------------------------
 * DEBUG-ONLY: global operator new/delete override for nanovg agge diagnosis.
 *
 * Purpose:
 *   The nanovg AGGE C++ backend uses `new T[]` inside pod_vector::grow(),
 *   which by default routes to libc malloc -> Zephyr __wrap__malloc_r ->
 *   os_mem_alloc(RAM_TYPE_DATA_ON), and dies with:
 *       os_mem_(alligned)_alloc failed! ram_type:0 alloc_size:0x8250
 *   when the 100 KB heap_sram cannot serve a mid-sized (~33 KB) request.
 *
 * This file installs *global* operator new/delete overrides that route
 * every C++ heap allocation to gui_lower_malloc(), which lives in the
 * 6 MB PSRAM tlsf pool. If the crash goes away with this file linked in,
 * the culprit is confirmed to be C++ operator new, not any raw malloc()
 * inside nanovg / honeygui.
 *
 * Scope:
 *   Global. Every `new` in the whole firmware image is affected while
 *   this .cpp is compiled in. Remove or #if 0 the file after diagnosis.
 * ---------------------------------------------------------------------------
 */

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "gui_api.h"
    /* Fallback log — DBG_DIRECT prints even before UART console is ready. */
    extern void DBG_DIRECT(const char *fmt, ...);
}

/* Toggle to 1 to trace every allocation size (very noisy). */
#define NVG_NEW_OVERRIDE_TRACE  0

/* Count how many allocations went through this override. Read via debugger
 * or expose to shell if needed. */
static volatile uint32_t g_nvg_new_count = 0;
static volatile uint32_t g_nvg_new_bytes = 0;

static inline void *nvg_new_alloc(size_t size)
{
    void *p = gui_malloc(size);
    g_nvg_new_count++;
    g_nvg_new_bytes += (uint32_t)size;
#if NVG_NEW_OVERRIDE_TRACE
    DBG_DIRECT("[new] size=%u ptr=%p (total_bytes=%u count=%u)",
               (unsigned)size, p,
               (unsigned)g_nvg_new_bytes, (unsigned)g_nvg_new_count);
#endif
    return p;
}

static inline void nvg_new_free(void *p)
{
    if (p != NULL)
    {
        gui_free(p);
    }
}

/* -------- non-throwing forms (Zephyr + newlib is -fno-exceptions) -------- */

void *operator new (size_t size)                 { return nvg_new_alloc(size); }
void *operator new[](size_t size)               { return nvg_new_alloc(size); }

void  operator delete (void *p)                  { nvg_new_free(p); }
void  operator delete[](void *p)                { nvg_new_free(p); }

/* C++14 sized-delete forms — the toolchain may emit these instead of the
 * unsized ones. Route them to the same free. */
void  operator delete (void *p, size_t)          { nvg_new_free(p); }
void  operator delete[](void *p, size_t)        { nvg_new_free(p); }

/* nothrow variants — some libstdc++ paths use these. */
namespace std { struct nothrow_t { }; }
void *operator new (size_t size, const std::nothrow_t &) noexcept
{ return nvg_new_alloc(size); }
void *operator new[](size_t size, const std::nothrow_t &) noexcept
{ return nvg_new_alloc(size); }
void  operator delete (void *p, const std::nothrow_t &) noexcept
{ nvg_new_free(p); }
void  operator delete[](void *p, const std::nothrow_t &) noexcept
{ nvg_new_free(p); }

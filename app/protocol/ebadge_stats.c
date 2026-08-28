/*
 * ebadge_stats.c - 1 Hz CPU / RAM usage line.  See ebadge_stats.h for why this
 * is a tick sink rather than a thread, why the CPU figure is a windowed delta,
 * and why the two HoneyGUI heaps are not read from here.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

#include "ebadge_stats.h"
#include "ebadge_task.h"
#include "ebadge_log.h"

#include <os_mem.h>

/* How often a line is emitted.  The tick arrives every EBADGE_TICK_MS (100 ms),
 * so this is a divide-by-N on that rather than a timer of its own. */
#define STATS_PERIOD_MS     1000U

/* Per-thread CPU breakdown alongside the summary line.
 *
 * OFF by default, and not because it is expensive to compute: the console is
 * shared with the shell (CONFIG_SHELL_BACKEND_SERIAL=y), and this build has
 * enough threads that a full dump at 1 Hz makes the shell unusable to type into.
 * Turn it on for a bring-up session where nobody is typing.
 *
 * Requires CONFIG_THREAD_MONITOR (already on in this build) for the iteration to
 * see anything, and CONFIG_THREAD_NAME (likewise) for the names to be printable.
 */
#ifndef EBADGE_STATS_PER_THREAD
#define EBADGE_STATS_PER_THREAD   0
#endif

/* What it would take to include the HoneyGUI heaps here.
 *
 * Reading them needs gui_mem_used()/gui_low_mem_used(), and both are only safe
 * on gui_task -- they walk their TLSF pool through an unlocked shared
 * accumulator, and they assert if the pool does not exist yet, which it does not
 * when this sink first fires.  So this is not a matter of adding an #include:
 * it needs a gui_register_server_hook() that samples into two plain uint32_t on
 * gui_task, which this sink then merely prints.  Left undone rather than done
 * unsafely; see the header. */
#ifndef EBADGE_STATS_WANT_GUI_HEAP
#define EBADGE_STATS_WANT_GUI_HEAP   0
#endif

/* Zephyr's k_malloc arena.  Declared the same way the kernel's own shell command
 * does it (zephyr/subsys/shell/modules/kernel_service.c:339) -- there is no
 * public accessor for it. */
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS) && (K_HEAP_MEM_POOL_SIZE > 0)
extern struct sys_heap _system_heap;
#define STATS_HAVE_SYS_HEAP   1
#else
#define STATS_HAVE_SYS_HEAP   0
#endif

static uint32_t s_last_ms;
static bool     s_primed;          /* first tick only establishes a baseline */

#ifdef CONFIG_SCHED_THREAD_USAGE
/* Previous CPU-wide cycle totals, for the windowed delta. */
static uint64_t s_prev_busy;       /* non-idle            */
static uint64_t s_prev_total;      /* non-idle + idle     */
#endif

/*----------------------------------------------------------------------------*
 *  CPU
 *----------------------------------------------------------------------------*/

#ifdef CONFIG_SCHED_THREAD_USAGE
/* Busy percent over the window, in tenths, so a mostly-idle system reads 0.4%
 * rather than rounding to 0 and looking like the counter is dead.
 *
 * The 64-bit intermediate is load-bearing: at ~40 MHz a one-second window is
 * ~4e7 cycles, and multiplying that by 1000 overflows a uint32_t at ~4.3e6. */
static uint32_t busy_permille(uint64_t busy_delta, uint64_t total_delta)
{
    if (total_delta == 0U)
    {
        return 0U;
    }
    return (uint32_t)((busy_delta * 1000U) / total_delta);
}

static void log_cpu(void)
{
    k_thread_runtime_stats_t now;

    if (k_thread_runtime_stats_all_get(&now) != 0)
    {
        EBADGE_WARN("stats: cpu read failed");
        return;
    }

    /* usage.c:151-167 -- for CPU-wide stats total_cycles is the non-idle sum and
     * execution_cycles is non-idle + idle.  So the ratio is the busy fraction
     * directly, with no need for the cycles-per-second constant. */
    uint64_t busy_delta  = now.total_cycles     - s_prev_busy;
    uint64_t total_delta = now.execution_cycles - s_prev_total;

    s_prev_busy  = now.total_cycles;
    s_prev_total = now.execution_cycles;

    uint32_t pm = busy_permille(busy_delta, total_delta);

    EBADGE_LOG("cpu %u.%u%% (busy %u/%u cyc)",
               (unsigned)(pm / 10U), (unsigned)(pm % 10U),
               (unsigned)busy_delta, (unsigned)total_delta);
}

#if EBADGE_STATS_PER_THREAD
static void thread_cb(const struct k_thread *thread, void *user)
{
    ARG_UNUSED(user);

    k_thread_runtime_stats_t st;

    if (k_thread_runtime_stats_get((k_tid_t)thread, &st) != 0)
    {
        return;
    }

    const char *name = k_thread_name_get((k_tid_t)thread);

    EBADGE_LOG("  thr %-12s %u cyc",
               (name != NULL) ? name : "?", (unsigned)st.total_cycles);
}
#endif /* EBADGE_STATS_PER_THREAD */
#endif /* CONFIG_SCHED_THREAD_USAGE */

/*----------------------------------------------------------------------------*
 *  RAM
 *----------------------------------------------------------------------------*/

static void log_ram(void)
{
#if STATS_HAVE_SYS_HEAP
    struct sys_memory_stats hs;
    bool have_sys = (sys_heap_runtime_stats_get(&_system_heap, &hs) == 0);
#endif

    /* os_mem_peek() returns FREE bytes, not used ones, and the pool total is not
     * exposed -- so this is printed as a free figure and the two domains are
     * labelled to keep that from being misread as usage. */
    size_t osif_data = os_mem_peek(OS_MEM_TYPE_DATA);
    size_t osif_buf  = os_mem_peek(OS_MEM_TYPE_BUFFER);

#if STATS_HAVE_SYS_HEAP
    if (have_sys)
    {
        EBADGE_LOG("ram k_malloc used=%u free=%u peak=%u | osif free data=%u buf=%u",
                   (unsigned)hs.allocated_bytes,
                   (unsigned)hs.free_bytes,
                   (unsigned)hs.max_allocated_bytes,
                   (unsigned)osif_data,
                   (unsigned)osif_buf);
        return;
    }
#endif
    EBADGE_LOG("ram osif free data=%u buf=%u",
               (unsigned)osif_data, (unsigned)osif_buf);
}

/*----------------------------------------------------------------------------*
 *  The sink
 *----------------------------------------------------------------------------*/

static void stats_tick(uint32_t now_ms)
{
    /* Unsigned difference, so a wrap of the millisecond counter costs one late
     * sample rather than stalling until it wraps back round -- the convention
     * the other sinks in this stack use. */
    if (!s_primed)
    {
        s_last_ms = now_ms;
        s_primed  = true;

#ifdef CONFIG_SCHED_THREAD_USAGE
        /* Baseline, so the first printed window is a real second rather than
         * "everything since boot" attributed to one interval. */
        k_thread_runtime_stats_t st;
        if (k_thread_runtime_stats_all_get(&st) == 0)
        {
            s_prev_busy  = st.total_cycles;
            s_prev_total = st.execution_cycles;
        }
#endif
        return;
    }

    if ((uint32_t)(now_ms - s_last_ms) < STATS_PERIOD_MS)
    {
        return;
    }
    s_last_ms = now_ms;

#ifdef CONFIG_SCHED_THREAD_USAGE
    log_cpu();
#if EBADGE_STATS_PER_THREAD
    k_thread_foreach_unlocked(thread_cb, NULL);
#endif
#endif
    log_ram();
}

void ebadge_stats_init(void)
{
#ifndef CONFIG_SCHED_THREAD_USAGE
    /* Not fatal: the RAM half still works and is the more common question.  Said
     * out loud because a silently CPU-less usage log looks like a broken counter
     * rather than a missing Kconfig. */
    EBADGE_WARN("stats: no CONFIG_SCHED_THREAD_USAGE, RAM only");
#endif
    ebadge_task_set_tick(stats_tick);
}

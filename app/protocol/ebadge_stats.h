/**
 * @file    ebadge_stats.h
 * @brief   Once-a-second CPU and RAM usage line on the console.
 *
 * A bring-up instrument, not a product feature: it answers "is something eating
 * the CPU" and "which heap is filling up" without a debugger attached, which are
 * the two questions that otherwise need one.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS RIDES THE EXISTING TICK INSTEAD OF OWNING A TIMER
 * ---------------------------------------------------------------------------
 * ebadge_task.c already runs a 100 ms timer whose message is fanned out to tick
 * sinks on l2_task, so a 1 Hz job is a counter on that, not a second timer and
 * certainly not a thread.  A thread would also be the wrong shape: measuring CPU
 * usage from a thread that exists only to measure it adds a context switch per
 * sample to the thing being measured.
 *
 * Zephyr's own thread analyzer can print this, but not at this rate --
 * CONFIG_THREAD_ANALYZER_AUTO_INTERVAL has a range of 5..3600 s, so 1 Hz is not
 * expressible through it.  Hence a sink here rather than a Kconfig line.
 *
 * ---------------------------------------------------------------------------
 * THE CPU NUMBER IS A WINDOW, NOT AN AVERAGE
 * ---------------------------------------------------------------------------
 * The kernel keeps cycle totals since boot.  Printing those directly gives a
 * number that converges and then barely moves, which is useless for spotting a
 * load spike -- by the time a busy second has been diluted by an hour of uptime
 * it is invisible.  So this keeps the previous sample and reports the delta over
 * the interval: the number describes the second just elapsed.
 *
 * The ratio is taken from the kernel's own two counters (total_cycles /
 * execution_cycles, which for CPU-wide stats are non-idle and non-idle+idle
 * respectively -- see zephyr/kernel/usage.c:151-167).  That deliberately avoids
 * sys_clock_hw_cycles_per_sec(), which would turn this into a wall-clock
 * calculation that has to agree with the tick period to be right.
 *
 * ---------------------------------------------------------------------------
 * FOUR HEAPS, FOUR APIS -- AND ONLY TWO OF THEM ARE SAFE TO READ HERE
 * ---------------------------------------------------------------------------
 * This SoC has no single "free RAM" figure.  There are four independent pools:
 *
 *   1. Zephyr's k_malloc pool (_system_heap)  -- sys_heap_runtime_stats_get()
 *   2. HoneyGUI upper heap, 30 KB of SRAM    -- gui_mem_used()
 *   3. HoneyGUI lower heap, 3 MB of PSRAM    -- gui_low_mem_used()
 *   4. RTK OSIF / BT heap                    -- os_mem_peek()
 *
 * (2) and (3) are NOT read from here, and the reason is worth stating because
 * calling them looks obviously correct.  Both walk their TLSF pool
 * (tlsf_walk_pool) accumulating into ONE shared file-scope global in
 * gui_api_os.c:30, with no lock anywhere in gui_malloc/gui_free.  Sampling them
 * from l2_task while gui_task allocates therefore races on both the accumulator
 * and the pool's own block list.  Worse, they GUI_ASSERT(tlsf != NULL), and this
 * sink starts ticking before those pools exist: ebadge_task_init() runs at
 * main.c:77 and gui_server_init() -- which is what creates them -- only at :103.
 * So the naive version does not merely risk a wrong number, it can assert on the
 * first sample of every boot.
 *
 * The right place for GUI heap figures is a hook on gui_task itself
 * (gui_register_server_hook), where the walk is serialised against the
 * allocations by construction.  Deliberately left out rather than done wrong;
 * EBADGE_STATS_WANT_GUI_HEAP below records what it would take.
 *
 * Note also that os_mem_peek() reports FREE bytes while sys_heap reports
 * ALLOCATED bytes.  Both are printed as they come rather than converted, since
 * the OSIF heap's total size is not exposed to compute the complement from.
 */
#ifndef _EBADGE_STATS_H_
#define _EBADGE_STATS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the 1 Hz usage log.  Call once from ebadge_task_init().
 *
 * Consumes one of the EBADGE_TICK_SINKS slots (the 5th of 6 as of writing).  A
 * sink that does not fit is dropped with an error and this silently stops
 * printing -- which is the correct failure for an instrument, but means the
 * slot budget is worth a glance before adding another one.
 *
 * Safe to call before any heap or thread of interest exists: the first sample
 * establishes the baseline and prints nothing.
 */
void ebadge_stats_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_STATS_H_ */

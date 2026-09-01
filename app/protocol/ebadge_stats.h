/**
 * @file    ebadge_stats.h
 * @brief   Periodic CPU and RAM usage line on the console (every 5 s).  OFF by
 *          default -- see EBADGE_STATS_ENABLE below.
 *
 * A bring-up instrument, not a product feature: it answers "is something eating
 * the CPU" and "which heap is filling up" without a debugger attached, which are
 * the two questions that otherwise need one.
 *
 * The period lives in ebadge_stats.c as STATS_PERIOD_MS.  It was 1 s originally
 * and is now 5 s, because at 1 Hz two lines of largely unchanging numbers scroll a
 * transfer's or a preview's own logs out of view.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS RIDES THE EXISTING TICK INSTEAD OF OWNING A TIMER
 * ---------------------------------------------------------------------------
 * ebadge_task.c already runs a 100 ms timer whose message is fanned out to tick
 * sinks on l2_task, so a periodic job is a counter on that, not a second timer and
 * certainly not a thread.  A thread would also be the wrong shape: measuring CPU
 * usage from a thread that exists only to measure it adds a context switch per
 * sample to the thing being measured.
 *
 * Zephyr's own thread analyzer can print something similar, and at 5 s it is now
 * within that module's reach -- CONFIG_THREAD_ANALYZER_AUTO_INTERVAL has a range
 * of 5..3600 s, so the original reason for hand-rolling this ("1 Hz is not
 * expressible there") no longer holds on the period alone.  What still holds is
 * the content: the analyzer reports per-thread stack and cycle totals, not the
 * windowed CPU delta below, and it knows nothing about the osif pools or the
 * k_malloc arena, which are the figures this exists to show.  Switching would mean
 * losing those to save a counter.
 *
 * ---------------------------------------------------------------------------
 * THE CPU NUMBER IS A WINDOW, NOT AN AVERAGE
 * ---------------------------------------------------------------------------
 * The kernel keeps cycle totals since boot.  Printing those directly gives a
 * number that converges and then barely moves, which is useless for spotting a
 * load spike -- by the time a busy second has been diluted by an hour of uptime
 * it is invisible.  So this keeps the previous sample and reports the delta over
 * the interval: the number describes the window just elapsed, currently 5 s.
 *
 * That window is also the resolution limit: a burst shorter than it is averaged
 * down rather than shown, so a 200 ms spike reads as 4% of a 5 s window.  Shorten
 * STATS_PERIOD_MS when hunting one.
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
 * @brief  Master switch for the whole instrument.  0 = compiled out (default).
 *
 * OFF by default because this is a bring-up tool and the console it prints to is
 * shared: the shell lives there (CONFIG_SHELL_BACKEND_SERIAL=y), and so does the
 * protocol stack's own logging, which is what someone reading the console during
 * a transfer or a preview is actually after.  Two lines every 5 s is not much,
 * but it is two lines of "nothing has changed" competing with the lines that did.
 * An instrument earns its console space while you are using it and not otherwise.
 *
 * Set to 1 for a bring-up session -- either edit this line, or define it on the
 * command line, since the #ifndef leaves it overridable:
 *
 *     west build ... -- -DEXTRA_CFLAGS=-DEBADGE_STATS_ENABLE=1
 *
 * WHAT TURNING IT OFF ACTUALLY SAVES, in order of how much anyone should care:
 *
 *   - The console lines.  This is the entire motivation; everything below is
 *     incidental.
 *   - One of the EBADGE_TICK_SINKS (6) slots.  With this off, three are taken
 *     (vbat and the two sessions) instead of four, so the budget noted in
 *     ebadge_task.c is one roomier.  Not a reason to leave it off -- there was
 *     headroom either way -- but it is why the count in that comment is stated
 *     as a range now.
 *   - A few hundred bytes of flash and two uint64_t of .bss.  Immaterial next to
 *     the 90%-full RAM region; mentioned only so nobody turns this on expecting
 *     to pay for it.
 *
 * What it does NOT save: CONFIG_SCHED_THREAD_USAGE stays on, so the kernel keeps
 * accumulating per-thread cycle counts at every context switch whether or not
 * anything reads them.  That accounting is the real (small) runtime cost of the
 * feature, and it is a Kconfig in prj.conf, not this macro.  Switching this off
 * makes the console quiet, not the scheduler cheaper.
 */
#ifndef EBADGE_STATS_ENABLE
#define EBADGE_STATS_ENABLE   0
#endif

/**
 * @brief  Register the periodic usage log.  Call once from ebadge_task_init().
 *
 * An empty function when EBADGE_STATS_ENABLE is 0, which is the default -- the
 * switch is handled here rather than at the call site so the wiring order in
 * ebadge_task_init() stays a flat readable sequence.
 *
 * When enabled, consumes one of the EBADGE_TICK_SINKS slots.  A sink that does
 * not fit is dropped with an error and this silently stops printing -- which is
 * the correct failure for an instrument, but means the slot budget is worth a
 * glance before adding another one.
 *
 * Safe to call before any heap or thread of interest exists: the first sample
 * establishes the baseline and prints nothing.
 */
void ebadge_stats_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_STATS_H_ */

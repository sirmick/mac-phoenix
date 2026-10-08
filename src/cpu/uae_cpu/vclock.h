/*
 *  vclock.h - deterministic virtual clock (--deterministic)
 *
 *  Time counted in executed 68k instructions instead of wall-clock time, so
 *  two runs of the same guest code see the same ticks, Microseconds, Time
 *  Manager firings and dates. Shared by the real-ROM and Executor backends
 *  (both run on this UAE core): cpu_do_check_ticks() advances it every
 *  quantum of instructions and calls vclock_poll_hook, where each backend
 *  fires its due interrupts on the CPU thread.
 *
 *  Before the traced application starts, every instruction counts. After
 *  that only application instructions count: those whose PC lies above the
 *  System heap and inside RAM (Process Manager heap: partitions, the app's
 *  code heap). ROM, System-heap patches and Executor's callback cells are
 *  outside, so a trap costs no time on either backend and the clock stays
 *  in step while the application's own control flow matches.
 *
 *  While an idle trap is active (WaitNextEvent, GetNextEvent, EventAvail,
 *  SystemTask, Delay) every instruction counts again, so a ROM idle loop
 *  waiting on Ticks sees time pass (Executor's native waits skip instead).
 *
 *  Virtual time = app instructions / VCLOCK_INSNS_PER_USEC + skipped time.
 *  Idle waits skip straight to the next deadline (vclock_skip_to) instead of
 *  blocking on the host clock. When the traced application first runs
 *  (vclock_rebase), both backends skip to the same VCLOCK_APP_START, so
 *  boot time before it (long on the ROM side) doesn't matter.
 */
#ifndef VCLOCK_H
#define VCLOCK_H

#include <stdint.h>

#define VCLOCK_INSNS_PER_USEC 20                /* nominal 20 MIPS */
#define VCLOCK_START_UNIX 1767225600ULL         /* 2026-01-01 00:00:00 UTC */
#define VCLOCK_START_MAC (VCLOCK_START_UNIX + 2082844800ULL)
#define VCLOCK_APP_START_USEC 100000000ULL      /* time when the app starts */

#ifdef __cplusplus
extern "C" {
#endif

extern int vclock_enabled;
extern void (*vclock_poll_hook)(void);
extern uint32_t vclock_app_lo, vclock_app_hi; /* app code region (refreshed per quantum) */
extern uint64_t vclock_app_insns;
extern int vclock_rebased;
extern int vclock_idle_depth;   /* idle traps active (WaitNextEvent, ...): count every instruction */

uint64_t vclock_usec(void);                     /* virtual microseconds since start */
void vclock_skip_to(uint64_t usec);             /* jump forward (idle) */
void vclock_advance_insns(uint64_t n);          /* called by the CPU loop */
static inline void vclock_count(uint32_t pc)    /* per instruction */
{
    /* Until the app starts every instruction counts (the ROM boot waits on
       ticks); after the rebase only the application's own. */
    if (!vclock_rebased || vclock_idle_depth || (pc >= vclock_app_lo && pc < vclock_app_hi))
        vclock_app_insns++;
}
void vclock_rebase(void);                       /* app start: skip to VCLOCK_APP_START_USEC once */
static inline uint32_t vclock_mac_time(void)    /* seconds since 1904 */
{
    return (uint32_t)(VCLOCK_START_MAC + vclock_usec() / 1000000);
}

#ifdef __cplusplus
}
#endif

#endif

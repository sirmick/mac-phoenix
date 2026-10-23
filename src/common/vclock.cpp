/*
 *  vclock.cpp - deterministic virtual clock (--deterministic); see vclock.h.
 *
 *  Core-independent: the 68k cores count instructions (vclock_count) and
 *  call vclock_quantum() every slice.
 */
#include "vclock.h"
#include "platform.h"

#include <cstdio>

int vclock_enabled = 0;
void (*vclock_poll_hook)(void) = nullptr;
uint32_t vclock_app_lo = 0xFFFFFFFF, vclock_app_hi = 0;
uint64_t vclock_app_insns = 0;
int vclock_rebased = 0;
int vclock_idle_depth = 0;
/* Executor's RAM is far below 1GB and its callback cells far above. */
uint32_t vclock_ram_top = 0x40000000;
static uint64_t vclock_skipped = 0;

uint64_t vclock_usec(void) { return vclock_app_insns / VCLOCK_INSNS_PER_USEC + vclock_skipped; }

/* Through the Platform's address translation, not its accessors (those
 * feed the A-trap trace's low-memory watch). */
static uint32_t vclock_peek(uint32_t a)
{
    const uint8_t *p = g_platform.mem_mac_to_host(a);
    return p ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3] : 0;
}

/* Per quantum: refresh the application region, then let the backend fire
 * whatever became due. The region starts where the System heap ends
 * (SysZone's bkLim, the zone header's first long, SysZone at $2A6) and
 * runs to the top of RAM. */
void vclock_quantum(void)
{
    uint32_t top = vclock_ram_top;
    uint32_t sys = vclock_peek(0x2A6);
    uint32_t lo = (sys >= 0x2000 && sys < top) ? vclock_peek(sys) : 0;
    if (lo > 0x2000 && lo < top) {
        vclock_app_lo = lo;
        vclock_app_hi = top;
    }
    if (vclock_poll_hook)
        vclock_poll_hook();
}

void vclock_rebase(void)
{
    if (vclock_rebased)
        return;
    vclock_rebased = 1;
    fprintf(stderr, "[vclock] app start at %.3f virtual s: rebased to %.0f s (app region $%08x-$%08x)\n",
            vclock_usec() / 1e6, VCLOCK_APP_START_USEC / 1e6, vclock_app_lo, vclock_app_hi);
    vclock_skip_to(VCLOCK_APP_START_USEC);
}

void vclock_skip_to(uint64_t usec)
{
    uint64_t now = vclock_usec();
    if (usec > now)
        vclock_skipped += usec - now;
}

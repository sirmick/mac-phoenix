/*
 * guest_watch.h - a write watchpoint on guest memory, for every writer.
 *
 * The page holding the watched bytes is made read-only; a write faults,
 * the fault handler lets it through under single-step, and the trap after
 * the instruction re-protects the page and reports the write when it hit
 * the watched bytes: old and new value, the host instruction, and the
 * guest context the backend describes (68k PC, registers, the last traps).
 * Writes from host C++ code and from every CPU core are caught alike, in
 * the IPC child as well as in-process. Linux x86-64 only (the single step
 * is the TF flag); elsewhere install fails.
 *
 * Caveats: a system call writing into the watched page (read() into a
 * guest buffer) fails with EFAULT instead of faulting; two threads writing
 * the page at once can slip one write past the protection; a rep movs
 * through the page traps once per iteration (slow, not wrong).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

struct GuestWatchHooks
{
    /* Print the guest CPU's state: PC, registers, code around the PC,
     * recent traps. May be null. */
    void (*describe)(FILE *out);
};

/* Watch [host_addr, host_addr + len) (len 1..16), reported as guest
 * address guest_addr. One watch at a time. Returns false when the
 * platform can't do it or the page can't be protected. */
bool guest_watch_install(void *host_addr, size_t len, uint32_t guest_addr,
                         const GuestWatchHooks &hooks);

/* Stop watching (the page is writable again). */
void guest_watch_remove();

/* Parse "ADDR[:LEN]" (hex ADDR, decimal LEN, default 4). Returns false on a
 * malformed string. */
bool guest_watch_parse(const char *spec, uint32_t *addr, size_t *len);

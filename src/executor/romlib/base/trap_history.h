/* The last A-line traps the dispatcher saw (trap word, guest PC), a ring
 * printed by the fatal reports (host signal, memcheck, --watch) so a
 * crash comes with what the guest was doing. Cheap enough to be always
 * on, unlike --executor-logtraps. */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstddef>

namespace Executor
{
void trap_history_record(uint16_t trap, uint32_t pc);

/* The last n traps, oldest first. */
void trap_history_dump(FILE *out, int n);

/* Moves of guest memory done on the host (BlockMove, the Memory Manager's
 * block moves; kept as guest addresses, $FFFFFFFF for host memory outside
 * the guest), and the last n of at least min_len bytes with a destination
 * below `below`, oldest first, with the trap count at each. Printed by the
 * fatal reports beside the traps. */
void move_history_record(const void *dst, const void *src, uint32_t len, const char *who);
void move_history_dump(FILE *out, int n, uint32_t min_len, uint32_t below = 0xFFFFFFFF);
}

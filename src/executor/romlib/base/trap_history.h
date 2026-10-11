/* The last A-line traps the dispatcher saw (trap word, guest PC), a ring
 * printed by the fatal reports (host signal, memcheck, --watch) so a
 * crash comes with what the guest was doing. Cheap enough to be always
 * on, unlike --executor-logtraps. */
#pragma once

#include <cstdint>
#include <cstdio>

namespace Executor
{
void trap_history_record(uint16_t trap, uint32_t pc);

/* The last n traps, oldest first. */
void trap_history_dump(FILE *out, int n);
}

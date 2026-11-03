#pragma once
/*
 * ppc_break.h - PowerPC breakpoints and instruction traces for Executor
 * sessions (--executor-ppc-break, POST /api/executor/ppc-break;
 * docs/executor/PPCTrace.md).
 *
 * A breakpoint is a guest address or a place in a PowerPC fragment, by
 * the fragment's name: resolved when the fragment is prepared, again for
 * each new instance. On a hit: the registers, the back chain, the stack,
 * and optionally the next N instructions with disassembly and the
 * registers each one changed; to stderr, and kept for the API.
 */
#include <string>

namespace Executor
{
/* Before the PowerPC core is made: generate its instruction calls. */
void ppc_break_arm();
bool ppc_break_armed();

/* SPEC: WHERE[@TRACE][*HITS][#MEM]...
 *   WHERE  $ADDR (or 0xADDR, ADDR: hex) | FRAGMENT+OFF | FRAGMENT:SECT+OFF
 *          (OFF hex; FRAGMENT the CFM name or the file's, sect default the
 *          code section) | !NAME (Executor's PowerPC routine NAME)
 *   TRACE  instructions to trace after a hit (default 0)
 *   HITS   hits to report (default 1; 0 every one)
 *   MEM    memory shown on a hit: rN[+-OFF][:LEN] or $ADDR[:LEN], hex; each
 *          leading ^ reads through the address once more
 * false with err set when SPEC is bad or the core can't. */
bool ppc_break_add(const std::string& spec, std::string& err);
bool ppc_break_remove(const std::string& spec);   /* "all": every one */

/* A fragment was prepared: resolve the breakpoints by name. */
void ppc_break_fragments_changed();

/* The breakpoints and the reports of recent hits as JSON (the API's
 * answer); hits after sequence number since. */
std::string ppc_break_json(unsigned since = 0);

/* The API's command: {"add": SPEC} / {"remove": SPEC} / {} as text
 * "add SPEC", "remove SPEC", "list [SINCE]"; JSON back. */
std::string ppc_break_command(const std::string& command);
}

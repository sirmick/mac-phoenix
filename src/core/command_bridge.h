/*
 * command_bridge.h - Host-side command dispatcher for controlling Mac OS
 *
 * Read commands (app name, window list) peek Mac memory directly from
 * the 60Hz IRQ — no Toolbox calls needed.
 *
 * Action commands (launch, quit) are handled by a guest-side agent app
 * installed in System Folder:Startup Items. It communicates via file I/O
 * on the ExtFS volume — no EmulOps, no hand-coded 68k.
 */

#ifndef COMMAND_BRIDGE_H
#define COMMAND_BRIDGE_H

#include <cstdint>
#include <string>

struct M68kRegisters;

enum class CmdType {
    GET_APP_NAME,       // Read CurApName (0x0910)
    GET_WINDOW_LIST,    // Walk WindowList (0x09D6)
    GET_TICKS,          // Read Ticks (0x016A)
    READ_MEMORY,        // Peek arbitrary address
};

struct CommandResult {
    bool done = false;
    int16_t err = 0;
    std::string data;
};

// Execute a read command immediately (safe from any thread when Mac memory is accessible)
CommandResult command_bridge_read(CmdType type, uint32_t addr = 0, uint32_t len = 0);

// Initialize host-side bridge state. Call once at startup, after
// EmulatorConfig has been finalized. Logs the bridge directory.
// No-op when bridge is disabled. Idempotent.
void command_bridge_init();

// The bridge dir as the guest sees it ("Host:MacPhoenix:<pid>": the first
// --extfs root is the Host volume), or "" when the bridge is off or its dir
// is not under that root.
std::string command_bridge_guest_dir();

// BridgeAgent.bin (MacBinary) in the source tree or the installed share
// folder, or "" if neither has it.
std::string command_bridge_agent_bin();

// Spawn a detached watchdog thread that waits until `finder_reached()`
// returns true, then waits grace_seconds for the BridgeAgent to write
// `bridge_heartbeat` into bridge_dir. Logs a warning if the agent never
// checks in. No-op when bridge is disabled or bridge_dir is empty.
#include <functional>
void command_bridge_start_watchdog(std::function<bool()> finder_reached,
                                   int grace_seconds = 15);

// Called from the 60Hz IRQ handler — advances PPC boot phase to Finder.
// The guest-side bridge agent is launched by Finder from Startup Items;
// the emulator does not inject anything.
void command_bridge_drain_from_irq(M68kRegisters* r);
void command_bridge_drain_from_irq_ppc(M68kRegisters* r);

#endif // COMMAND_BRIDGE_H

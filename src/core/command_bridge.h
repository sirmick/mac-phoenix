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

// Called from the 60Hz IRQ handler — advances PPC boot phase to Finder.
// The guest-side bridge agent is launched by Finder from Startup Items;
// the emulator does not inject anything.
void command_bridge_drain_from_irq(M68kRegisters* r);
void command_bridge_drain_from_irq_ppc(M68kRegisters* r);

#endif // COMMAND_BRIDGE_H

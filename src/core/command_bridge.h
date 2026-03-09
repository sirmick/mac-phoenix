/*
 * command_bridge.h - Host-side command dispatcher for controlling Mac OS
 *
 * Commands are submitted from API handlers (any thread) and executed
 * during the 60Hz IRQ tick on the emulator thread.
 *
 * Read commands (app name, window list) peek Mac memory directly.
 * Trap commands (launch, quit) call Execute68kTrap during IRQ.
 */

#ifndef COMMAND_BRIDGE_H
#define COMMAND_BRIDGE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <string>

struct M68kRegisters;

enum class CmdType {
    // Read commands — peek Mac memory, no trap call needed
    GET_APP_NAME,       // Read CurApName (0x0910)
    GET_WINDOW_LIST,    // Walk WindowList (0x09D6)
    GET_TICKS,          // Read Ticks (0x016A)
    READ_MEMORY,        // Peek arbitrary address

    // Trap commands — Execute68kTrap on next IRQ tick
    LAUNCH_APP,         // _Launch (0xA9F2) with app path
    QUIT_APP,           // _ExitToShell (0xA9F4)
};

struct Command {
    uint32_t id = 0;
    CmdType type = CmdType::GET_APP_NAME;
    std::string arg;        // path for LAUNCH_APP, etc.
    uint32_t addr = 0;      // for READ_MEMORY
    uint32_t len = 0;       // for READ_MEMORY
};

struct CommandResult {
    uint32_t id = 0;
    bool done = false;
    int16_t err = 0;        // Mac OS error code (0 = noErr)
    std::string data;       // JSON or text result
};

class CommandBridge {
public:
    // Submit a command (called from any thread).
    // Returns command ID for retrieving the result.
    uint32_t submit(Command cmd);

    // Wait for a command result (called from API handler thread).
    // Returns true if result arrived before timeout.
    bool wait_result(uint32_t id, CommandResult& out, int timeout_ms = 5000);

    // Drain pending commands (called from 60Hz IRQ handler on emulator thread).
    // Processes one command per call to avoid stalling the IRQ.
    void drain(M68kRegisters* r);

    // Execute a read command immediately without queuing.
    // Safe to call from any thread when Mac memory is accessible.
    static CommandResult execute_read(CmdType type, uint32_t addr = 0, uint32_t len = 0);

    // Execute one command (read or trap). Called from drain() and shm drain.
    void execute_one_public(const Command& cmd, M68kRegisters* r, CommandResult& result);

private:

    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Command> pending_;
    std::map<uint32_t, CommandResult> results_;
    std::atomic<uint32_t> next_id_{1};
};

// Global instance
extern CommandBridge g_command_bridge;

#endif // COMMAND_BRIDGE_H

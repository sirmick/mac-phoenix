/*
 * command_bridge.cpp - Host-side command dispatcher for controlling Mac OS
 *
 * Read commands (app name, window list, memory) peek Mac memory directly
 * from the 60Hz IRQ — safe because no Toolbox calls are needed.
 *
 * Action commands (launch, quit) use a mailbox in ScratchMem:
 *   1. IRQ handler writes the command to the mailbox
 *   2. A jGNEFilter (installed once at boot) checks the mailbox every event loop
 *   3. When found, it triggers M68K_EMUL_OP_CMD_DISPATCH (in app context)
 *   4. The EmulOp handler calls the Toolbox trap safely and writes the result
 *
 * The jGNEFilter is 28 bytes of 68k code living in ScratchMem.
 */

#include "command_bridge.h"
#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/m68k_registers.h"
#include "../common/include/platform.h"
#include "../common/include/emul_op.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <atomic>
#include <algorithm>

// Global instance (used for in-process mode, kept for compatibility)
CommandBridge g_command_bridge;

// ScratchMem layout
static const uint32_t SCRATCH_BASE    = 0x02100000;
static const uint32_t FILTER_CODE     = SCRATCH_BASE + 0x400;  // jGNEFilter (50 bytes)
static const uint32_t MAILBOX         = SCRATCH_BASE + 0x800;  // command mailbox

// Mailbox offsets
static const uint32_t MB_CMD_FLAG     = MAILBOX + 0;    // uint8: 1 = command pending
static const uint32_t MB_RESULT_FLAG  = MAILBOX + 1;    // uint8: 1 = result ready
static const uint32_t MB_CMD_TYPE     = MAILBOX + 2;    // int16: 1=LAUNCH, 2=QUIT
static const uint32_t MB_LAUNCH_READY = MAILBOX + 0x80; // uint8: 1 = LPB prepared, filter should _Launch inline
static const uint32_t MB_LAUNCH_LPB   = MAILBOX + 0x84; // uint32: LaunchParamBlockRec pointer
static const uint32_t MB_RESULT_ERR   = MAILBOX + 4;    // int16: Mac OS error code
static const uint32_t MB_CMD_ARG      = MAILBOX + 6;    // Pascal string (256 bytes)
static const uint32_t MB_RESULT_DATA  = MAILBOX + 0x106; // result text (256 bytes)

// Mailbox command types
static const int16_t MB_CMD_LAUNCH = 1;
static const int16_t MB_CMD_QUIT   = 2;

// jGNEFilter low memory global
static const uint32_t LM_JGNEFILTER = 0x29A;

// Whether the jGNEFilter has been installed
static std::atomic<bool> g_filter_installed{false};

// ============================================================================
// jGNEFilter installation
// ============================================================================

/*
 * Install a jGNEFilter in ScratchMem.
 *
 * The filter runs in real Finder app context (called from GetNextEvent) on
 * every event loop iteration. Two responsibilities:
 *   1. If MB_CMD_FLAG is set, trigger an EmulOp to run the C++ command
 *      dispatcher (handles read commands + launch-prep).
 *   2. If MB_LAUNCH_READY is set, execute _Launch INLINE with the LPB
 *      pointer from MB_LAUNCH_LPB. This runs _Launch as a normal 68k
 *      instruction in the filter's own context — no Execute68kTrap
 *      reentry, no Process Manager deadlock.
 *
 * 68k code at FILTER_CODE (50 bytes):
 *
 *   0x00: 207C 0210 0800  MOVEA.L  #MAILBOX, A0
 *   0x06: 4A10            TST.B    (A0)           ; MB_CMD_FLAG?
 *   0x08: 6702            BEQ.S    +2 → 0x0C
 *   0x0A: 71xx            EmulOp   CMD_DISPATCH
 *   0x0C: 207C 0210 0800  MOVEA.L  #MAILBOX, A0   ; reload (EmulOp may clobber)
 *   0x12: 4A28 0080       TST.B    0x80(A0)       ; MB_LAUNCH_READY?
 *   0x16: 670A            BEQ.S    +10 → 0x22
 *   0x18: 4228 0080       CLR.B    0x80(A0)       ; clear ready flag
 *   0x1C: 2068 0084       MOVEA.L  0x84(A0), A0   ; A0 = LPB ptr (MB_LAUNCH_LPB)
 *   0x20: A9F2            _Launch                 ; INLINE launch — no reentry!
 *   0x22: 207A 000A       MOVEA.L  oldFilter(PC), A0
 *   0x26: 4A88            TST.L    A0
 *   0x28: 6702            BEQ.S    +2 → 0x2C
 *   0x2A: 4ED0            JMP      (A0)
 *   0x2C: 4E75            RTS
 *   0x2E: 0000 0000       oldFilter: DC.L 0
 */
static void install_jgne_filter() {
    if (g_filter_installed.load(std::memory_order_acquire)) return;
    if (!RAMBaseHost || RAMSize == 0) return;

    uint16_t emulop = platform_make_emulop(M68K_EMUL_OP_CMD_DISPATCH);

    uint32_t p = FILTER_CODE;
    WriteMacInt16(p + 0x00, 0x207C);      // MOVEA.L #imm, A0
    WriteMacInt32(p + 0x02, MAILBOX);      //   #MAILBOX
    WriteMacInt16(p + 0x06, 0x4A10);      // TST.B (A0)              — MB_CMD_FLAG
    WriteMacInt16(p + 0x08, 0x6702);      // BEQ.S +2
    WriteMacInt16(p + 0x0A, emulop);      // EmulOp CMD_DISPATCH
    WriteMacInt16(p + 0x0C, 0x207C);      // MOVEA.L #imm, A0        — reload
    WriteMacInt32(p + 0x0E, MAILBOX);      //   #MAILBOX
    WriteMacInt16(p + 0x12, 0x4A28);      // TST.B d(A0)
    WriteMacInt16(p + 0x14, 0x0080);      //   d=0x80 (MB_LAUNCH_READY)
    WriteMacInt16(p + 0x16, 0x670A);      // BEQ.S +10 → chain
    WriteMacInt16(p + 0x18, 0x4228);      // CLR.B d(A0)
    WriteMacInt16(p + 0x1A, 0x0080);      //   d=0x80
    WriteMacInt16(p + 0x1C, 0x2068);      // MOVEA.L d(A0), A0
    WriteMacInt16(p + 0x1E, 0x0084);      //   d=0x84 (MB_LAUNCH_LPB)
    WriteMacInt16(p + 0x20, 0xA9F2);      // _Launch                 — INLINE, app ctx
    WriteMacInt16(p + 0x22, 0x207A);      // MOVEA.L d(PC), A0
    WriteMacInt16(p + 0x24, 0x000A);      //   d → oldFilter at 0x2E
    WriteMacInt16(p + 0x26, 0x4A88);      // TST.L A0
    WriteMacInt16(p + 0x28, 0x6702);      // BEQ.S +2
    WriteMacInt16(p + 0x2A, 0x4ED0);      // JMP (A0)
    WriteMacInt16(p + 0x2C, 0x4E75);      // RTS
    // Save old filter and install ours
    uint32_t old_filter = ReadMacInt32(LM_JGNEFILTER);
    WriteMacInt32(p + 0x2E, old_filter);  // oldFilter storage
    WriteMacInt32(LM_JGNEFILTER, FILTER_CODE);

    // Clear mailbox
    WriteMacInt8(MB_CMD_FLAG, 0);
    WriteMacInt8(MB_RESULT_FLAG, 0);
    WriteMacInt8(MB_LAUNCH_READY, 0);
    WriteMacInt32(MB_LAUNCH_LPB, 0);

    g_filter_installed.store(true, std::memory_order_release);
    fprintf(stderr, "[CommandBridge] jGNEFilter installed at 0x%08X (old=0x%08X, emulop=0x%04X)\n",
            FILTER_CODE, old_filter, emulop);
}

// ============================================================================
// EmulOp handler — called from jGNEFilter in application context
// ============================================================================

/*
 * Handle M68K_EMUL_OP_CMD_DISPATCH.
 * Called from the jGNEFilter during GetNextEvent/WaitNextEvent,
 * so we're in full application context — Toolbox calls are safe.
 */
void command_bridge_dispatch(M68kRegisters* /*r*/) {
    uint8_t cmd_flag = ReadMacInt8(MB_CMD_FLAG);
    if (!cmd_flag) return;

    int16_t cmd_type = static_cast<int16_t>(ReadMacInt16(MB_CMD_TYPE));

    // Clear command flag immediately to prevent re-entry
    WriteMacInt8(MB_CMD_FLAG, 0);

    switch (cmd_type) {

    case MB_CMD_LAUNCH: {
        // App path is a Pascal string at MB_CMD_ARG in the form "Volume:Name".
        //
        // Strategy:
        //   1. Iterate mounted volumes via _HGetVInfo to resolve "Volume" -> vRefNum
        //   2. Build an FSSpec for the leaf at the volume root
        //   3. Build an extended LaunchParamBlockRec with launchContinue
        //   4. Hand off to the filter (MB_LAUNCH_READY / MB_LAUNCH_LPB) which
        //      executes _Launch inline in real Finder app context — calling
        //      Execute68kTrap(_Launch) here would deadlock the Process Manager.

        uint8_t namelen = ReadMacInt8(MB_CMD_ARG);
        char name[256];
        for (int i = 0; i < namelen && i < 255; i++)
            name[i] = static_cast<char>(ReadMacInt8(MB_CMD_ARG + 1 + i));
        name[namelen] = '\0';

        // --- Step 1: Resolve "Volume:..." path to (vRefNum, leaf) ---
        // Walks mounted volumes with _HGetVInfo (HParamBlockRec):
        //   +16 ioResult, +18 ioNamePtr, +22 ioVRefNum, +28 ioVolIndex
        uint32_t pb_addr       = SCRATCH_BASE + 0xC00;
        uint32_t fsspec_addr   = SCRATCH_BASE + 0xD00;
        uint32_t vol_name_addr = SCRATCH_BASE + 0xE80;

        int colon_pos = -1;
        for (int i = 0; i < namelen; i++) {
            if (name[i] == ':') { colon_pos = i; break; }
        }

        int16_t  vRefNum    = 0;
        uint32_t parID      = 2;  // HFS root dir ID
        int      leaf_start = 0;

        if (colon_pos >= 0) {
            char volname[64];
            int vnlen = colon_pos > 63 ? 63 : colon_pos;
            for (int i = 0; i < vnlen; i++) volname[i] = name[i];
            volname[vnlen] = '\0';

            bool found = false;
            for (int16_t idx = 1; idx < 16 && !found; idx++) {
                for (int i = 0; i < 108; i++) WriteMacInt8(pb_addr + i, 0);
                WriteMacInt8(vol_name_addr, 0);
                WriteMacInt32(pb_addr + 18, vol_name_addr);
                WriteMacInt16(pb_addr + 22, 0);
                WriteMacInt16(pb_addr + 28, idx);

                M68kRegisters vi_regs;
                memset(&vi_regs, 0, sizeof(vi_regs));
                vi_regs.a[0] = pb_addr;
                Execute68kTrap(0xA207, &vi_regs);  // _HGetVInfo

                if (ReadMacInt16(pb_addr + 16) != 0) break;

                uint8_t rvnlen = ReadMacInt8(vol_name_addr);
                if (rvnlen == (uint8_t)vnlen) {
                    bool match = true;
                    for (int i = 0; i < vnlen; i++) {
                        if (ReadMacInt8(vol_name_addr + 1 + i) != (uint8_t)volname[i]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        vRefNum = static_cast<int16_t>(ReadMacInt16(pb_addr + 22));
                        found = true;
                    }
                }
            }

            if (!found) {
                WriteMacInt16(MB_RESULT_ERR, -35);  // nsvErr
                WriteMacInt8(MB_RESULT_FLAG, 1);
                break;
            }
            leaf_start = colon_pos + 1;
        }

        uint8_t leaf_len = namelen - leaf_start;
        if (leaf_len > 63) leaf_len = 63;

        // Build FSSpec: vRefNum(2) + parID(4) + name[64]
        WriteMacInt16(fsspec_addr + 0, vRefNum);
        WriteMacInt32(fsspec_addr + 2, parID);
        WriteMacInt8(fsspec_addr + 6, leaf_len);
        for (int i = 0; i < leaf_len; i++)
            WriteMacInt8(fsspec_addr + 7 + i, static_cast<uint8_t>(name[leaf_start + i]));

        // --- Step 2: Build extended LaunchParamBlockRec ---
        // Layout from Inside Macintosh: Processes, Ch.2:
        //   +0  reserved1              (LongInt)       = 0
        //   +4  reserved2              (Word)          = 0
        //   +6  launchBlockID          (Word)          = 'LC' = 0x4C43
        //   +8  launchEPBLength        (LongInt)       = extendedBlockLen = sizeof-12 = 34
        //   +12 launchFileFlags        (Word)          = 0
        //   +14 launchControlFlags     (LongInt)       = launchContinue | launchNoFileFlags
        //   +18 launchAppSpec          (FSSpecPtr)     = FSSpec ptr
        //   +22 launchProcessSN        (8 bytes)       = 0 (return slot)
        //   +30 launchPreferredSize    (LongInt)       = 0
        //   +34 launchMinimumSize      (LongInt)       = 0
        //   +38 launchAvailableSize    (LongInt)       = 0
        //   +42 launchAppParameters    (AppParamsPtr)  = 0
        // Total size: 46 bytes
        const int LPB_SIZE = 48;   // round up
        uint32_t lpb_addr = SCRATCH_BASE + 0xE00;
        for (int i = 0; i < LPB_SIZE; i++)
            WriteMacInt8(lpb_addr + i, 0);
        WriteMacInt16(lpb_addr + 6, 0x4C43);              // launchBlockID = 'LC'
        WriteMacInt32(lpb_addr + 8, 34);                   // launchEPBLength (46 - 12)
        WriteMacInt16(lpb_addr + 12, 0);                   // launchFileFlags
        WriteMacInt32(lpb_addr + 14, 0x4000 | 0x0800);     // launchContinue | launchNoFileFlags
        WriteMacInt32(lpb_addr + 18, fsspec_addr);         // launchAppSpec
        // +22..+29 launchProcessSN (zeroed)
        // +30..+41 sizes (zeroed → use app SIZE resource defaults)
        WriteMacInt32(lpb_addr + 42, 0);                   // launchAppParameters = none

        // --- Step 3: Hand off to filter for inline _Launch (no reentry) ---
        //
        // Calling Execute68kTrap(_Launch) from here would deadlock: Process
        // Manager needs Finder to yield via WaitNextEvent to schedule the new
        // app, but Finder would be blocked inside our EmulOp → Execute68kTrap
        // call. So instead of calling _Launch ourselves, we leave a note for
        // the filter: set MB_LAUNCH_LPB and MB_LAUNCH_READY. When this EmulOp
        // handler returns, the filter (still running in real Finder app
        // context from GetNextEvent) sees the ready flag and executes _Launch
        // inline as a normal 68k instruction. No reentry, no deadlock.
        //
        // The host polls CurApName (via IRQ-safe GET_APP_NAME read command)
        // to observe when MacTestSuite starts and exits back to Finder.
        WriteMacInt32(MB_LAUNCH_LPB, lpb_addr);
        WriteMacInt8(MB_LAUNCH_READY, 1);

        WriteMacInt16(MB_RESULT_ERR, 0);
        WriteMacInt8(MB_RESULT_FLAG, 1);
        break;
    }

    case MB_CMD_QUIT: {
        fprintf(stderr, "[CommandBridge] INIT quitting current app\n");

        WriteMacInt16(MB_RESULT_ERR, 0);
        WriteMacInt8(MB_RESULT_FLAG, 1);

        M68kRegisters quit_regs;
        memset(&quit_regs, 0, sizeof(quit_regs));
        Execute68kTrap(0xA9F4, &quit_regs);  // _ExitToShell — doesn't return
        break;
    }

    default:
        fprintf(stderr, "[CommandBridge] Unknown mailbox command: %d\n", cmd_type);
        WriteMacInt16(MB_RESULT_ERR, -1);
        WriteMacInt8(MB_RESULT_FLAG, 1);
        break;
    }
}

// ============================================================================
// In-process queue (legacy, kept for compatibility)
// ============================================================================

uint32_t CommandBridge::submit(Command cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    cmd.id = next_id_.fetch_add(1);
    pending_.push(std::move(cmd));
    return cmd.id;
}

bool CommandBridge::wait_result(uint32_t id, CommandResult& out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        auto it = results_.find(id);
        if (it != results_.end()) {
            out = std::move(it->second);
            results_.erase(it);
            return true;
        }
        if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            return false;
        }
    }
}

void CommandBridge::drain(M68kRegisters* r) {
    Command cmd;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) return;
        cmd = std::move(pending_.front());
        pending_.pop();
    }

    CommandResult result;
    result.id = cmd.id;
    result.done = true;

    execute_one_public(cmd, r, result);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_[cmd.id] = std::move(result);
    }
    cv_.notify_all();
}

// ============================================================================
// Read commands — peek Mac memory, no Toolbox calls needed
// ============================================================================

CommandResult CommandBridge::execute_read(CmdType type, uint32_t addr, uint32_t len) {
    CommandResult result;
    result.done = true;

    if (!RAMBaseHost || RAMSize == 0) {
        result.err = -1;
        result.data = "";
        return result;
    }

    switch (type) {

    case CmdType::GET_APP_NAME: {
        uint8_t namelen = ReadMacInt8(0x0910);
        if (namelen > 31) namelen = 31;
        char name[32];
        for (int i = 0; i < namelen; i++)
            name[i] = static_cast<char>(ReadMacInt8(0x0911 + i));
        name[namelen] = '\0';
        result.data = name;
        break;
    }

    case CmdType::GET_TICKS: {
        uint32_t ticks = ReadMacInt32(0x016A);
        result.data = std::to_string(ticks);
        break;
    }

    case CmdType::GET_WINDOW_LIST: {
        std::string json = "[";
        uint32_t wp = ReadMacInt32(0x09D6);
        bool first = true;
        int limit = 50;

        while (wp && wp < 0x02000000 && limit-- > 0) {
            if (!first) json += ",";
            first = false;

            std::string title;
            uint32_t title_handle = ReadMacInt32(wp + 134);
            if (title_handle) {
                uint32_t title_ptr = ReadMacInt32(title_handle);
                if (title_ptr && title_ptr < 0x02000000) {
                    uint8_t tlen = ReadMacInt8(title_ptr);
                    if (tlen > 0) {
                        for (int i = 0; i < tlen; i++) {
                            char c = static_cast<char>(ReadMacInt8(title_ptr + 1 + i));
                            if (c == '"' || c == '\\') json += "\\";
                            title += c;
                        }
                    }
                }
            }

            int16_t top = static_cast<int16_t>(ReadMacInt16(wp + 16));
            int16_t left = static_cast<int16_t>(ReadMacInt16(wp + 18));
            int16_t bottom = static_cast<int16_t>(ReadMacInt16(wp + 20));
            int16_t right = static_cast<int16_t>(ReadMacInt16(wp + 22));
            bool visible = ReadMacInt8(wp + 110) != 0;

            json += "{\"title\":\"" + title + "\""
                 +  ",\"rect\":[" + std::to_string(left) + ","
                 +  std::to_string(top) + ","
                 +  std::to_string(right) + ","
                 +  std::to_string(bottom) + "]"
                 +  ",\"visible\":" + (visible ? "true" : "false") + "}";

            wp = ReadMacInt32(wp + 144);
        }
        json += "]";
        result.data = json;
        break;
    }

    case CmdType::READ_MEMORY: {
        uint32_t read_len = std::min(len, (uint32_t)1024);
        std::string hex;
        hex.reserve(read_len * 2);
        for (uint32_t i = 0; i < read_len; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x", ReadMacInt8(addr + i));
            hex += buf;
        }
        result.data = hex;
        break;
    }

    default:
        result.err = -1;
        result.data = "not a read command";
        break;
    }

    return result;
}

// ============================================================================
// Command execution — read commands direct, action commands via mailbox
// ============================================================================

void CommandBridge::execute_one_public(const Command& cmd, M68kRegisters* /*r*/, CommandResult& result) {
    switch (cmd.type) {

    case CmdType::GET_APP_NAME:
    case CmdType::GET_TICKS:
    case CmdType::GET_WINDOW_LIST:
    case CmdType::READ_MEMORY:
        result = execute_read(cmd.type, cmd.addr, cmd.len);
        result.id = cmd.id;
        break;

    case CmdType::LAUNCH_APP: {
        const std::string& path = cmd.arg;
        auto plen = static_cast<uint8_t>(std::min<size_t>(path.size(), 255));

        // No pre-validation here — _GetFileInfo can't parse "Volume:File" paths
        // and we're in IRQ context where full Toolbox calls aren't safe.
        // The dispatcher (Side B, app context) does volume-aware lookup and
        // reports errors via MB_RESULT_ERR.
        //
        // Write path to mailbox and set command flag.
        // The jGNEFilter will pick it up on the next event loop iteration
        // and execute _Launch in application context.
        WriteMacInt8(MB_RESULT_FLAG, 0);
        WriteMacInt16(MB_CMD_TYPE, MB_CMD_LAUNCH);
        WriteMacInt8(MB_CMD_ARG, plen);
        for (int i = 0; i < plen; i++)
            WriteMacInt8(MB_CMD_ARG + 1 + i, static_cast<uint8_t>(path[i]));
        WriteMacInt8(MB_CMD_FLAG, 1);  // signal to jGNEFilter

        result.err = 0;
        result.data = "launched";
        fprintf(stderr, "[CommandBridge] Launch queued for INIT: %s\n", path.c_str());
        break;
    }

    case CmdType::QUIT_APP: {
        WriteMacInt8(MB_RESULT_FLAG, 0);
        WriteMacInt16(MB_CMD_TYPE, MB_CMD_QUIT);
        WriteMacInt8(MB_CMD_FLAG, 1);

        result.err = 0;
        result.data = "quit";
        fprintf(stderr, "[CommandBridge] Quit queued for INIT\n");
        break;
    }

    }  // switch
}

// ============================================================================
// Entry point from emul_op.cpp IRQ handler
// ============================================================================

void command_bridge_drain_from_irq(M68kRegisters* r) {
    // Install jGNEFilter once (first IRQ after Mac has started)
    if (!g_filter_installed.load(std::memory_order_acquire)) {
        install_jgne_filter();
    }

    // Drain in-process command queue
    g_command_bridge.drain(r);
}

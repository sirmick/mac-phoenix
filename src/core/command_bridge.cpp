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
#include "shared_state.h"
#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/m68k_registers.h"
#include "../common/include/platform.h"
#include "../common/include/emul_op.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>

// Global instance (used for in-process mode, kept for compatibility)
CommandBridge g_command_bridge;

// Child-side shared state pointer (set in cpu_process.cpp)
extern SharedState* g_child_shared_state;

// ScratchMem layout
static const uint32_t SCRATCH_BASE    = 0x02100000;
static const uint32_t FILTER_CODE     = SCRATCH_BASE + 0x400;  // jGNEFilter (28 bytes)
static const uint32_t MAILBOX         = SCRATCH_BASE + 0x800;  // command mailbox

// Mailbox offsets
static const uint32_t MB_CMD_FLAG     = MAILBOX + 0;    // uint8: 1 = command pending
static const uint32_t MB_RESULT_FLAG  = MAILBOX + 1;    // uint8: 1 = result ready
static const uint32_t MB_CMD_TYPE     = MAILBOX + 2;    // int16: 1=LAUNCH, 2=QUIT
static const uint32_t MB_RESULT_ERR   = MAILBOX + 4;    // int16: Mac OS error code
static const uint32_t MB_CMD_ARG      = MAILBOX + 6;    // Pascal string (256 bytes)
static const uint32_t MB_RESULT_DATA  = MAILBOX + 0x106; // result text (256 bytes)

// Mailbox command types
static const int16_t MB_CMD_LAUNCH = 1;
static const int16_t MB_CMD_QUIT   = 2;

// jGNEFilter low memory global
static const uint32_t LM_JGNEFILTER = 0x29A;

// Whether the jGNEFilter has been installed
static bool g_filter_installed = false;

// ============================================================================
// jGNEFilter installation
// ============================================================================

/*
 * Install a tiny jGNEFilter in ScratchMem.
 * The filter checks the mailbox every event loop iteration.
 * If a command is pending, it triggers M68K_EMUL_OP_CMD_DISPATCH.
 *
 * 68k code (28 bytes at FILTER_CODE):
 *
 *   0x00: 207C 0210 0800  MOVEA.L  #MAILBOX, A0
 *   0x06: 4A10            TST.B    (A0)           ; cmd_flag set?
 *   0x08: 6702            BEQ.S    chain          ; no -> skip
 *   0x0A: 71xx            EmulOp   CMD_DISPATCH   ; handle command (app context)
 *   0x0C: 207A 000A       MOVEA.L  oldFilter(PC), A0
 *   0x10: 4A88            TST.L    A0             ; old filter exists?
 *   0x12: 6702            BEQ.S    done
 *   0x14: 4ED0            JMP      (A0)           ; chain to old filter
 *   0x16: 4E75            RTS
 *   0x18: 0000 0000       oldFilter: DC.L 0
 */
static void install_jgne_filter() {
    if (g_filter_installed) return;
    if (!RAMBaseHost || RAMSize == 0) return;

    uint16_t emulop = platform_make_emulop(M68K_EMUL_OP_CMD_DISPATCH);

    // Write 68k code to ScratchMem
    uint32_t p = FILTER_CODE;
    WriteMacInt16(p + 0x00, 0x207C);      // MOVEA.L #imm, A0
    WriteMacInt32(p + 0x02, MAILBOX);      // #MAILBOX
    WriteMacInt16(p + 0x06, 0x4A10);      // TST.B (A0)
    WriteMacInt16(p + 0x08, 0x6702);      // BEQ.S +2
    WriteMacInt16(p + 0x0A, emulop);      // EmulOp CMD_DISPATCH
    WriteMacInt16(p + 0x0C, 0x207A);      // MOVEA.L d(PC), A0
    WriteMacInt16(p + 0x0E, 0x000A);      // offset to oldFilter: PC+2+0x0A = 0x0C+2+0x0A = 0x18
    WriteMacInt16(p + 0x10, 0x4A88);      // TST.L A0
    WriteMacInt16(p + 0x12, 0x6702);      // BEQ.S +2
    WriteMacInt16(p + 0x14, 0x4ED0);      // JMP (A0)
    WriteMacInt16(p + 0x16, 0x4E75);      // RTS
    // Save old filter and install ours
    uint32_t old_filter = ReadMacInt32(LM_JGNEFILTER);
    WriteMacInt32(p + 0x18, old_filter);  // oldFilter storage
    WriteMacInt32(LM_JGNEFILTER, FILTER_CODE);

    // Clear mailbox
    WriteMacInt8(MB_CMD_FLAG, 0);
    WriteMacInt8(MB_RESULT_FLAG, 0);

    g_filter_installed = true;
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
void command_bridge_dispatch(M68kRegisters* r) {
    uint8_t cmd_flag = ReadMacInt8(MB_CMD_FLAG);
    if (!cmd_flag) return;

    int16_t cmd_type = (int16_t)ReadMacInt16(MB_CMD_TYPE);

    // Clear command flag immediately to prevent re-entry
    WriteMacInt8(MB_CMD_FLAG, 0);

    switch (cmd_type) {

    case MB_CMD_LAUNCH: {
        // App path is a Pascal string at MB_CMD_ARG.
        // Already validated by the IRQ handler (_GetFileInfo passed).
        //
        // Strategy:
        //   1. FSMakeFSSpec to convert path -> FSSpec
        //   2. Build extended LaunchParamBlockRec with launchContinue
        //   3. Call _Launch (0xA9F2) with the param block
        //
        // The extended _Launch returns an error code instead of bombing.

        uint8_t namelen = ReadMacInt8(MB_CMD_ARG);
        char name[256];
        for (int i = 0; i < namelen && i < 255; i++)
            name[i] = (char)ReadMacInt8(MB_CMD_ARG + 1 + i);
        name[namelen] = '\0';
        fprintf(stderr, "[CommandBridge] INIT launching: %s\n", name);

        // --- Step 1: FSMakeFSSpec ---
        // FSMakeFSSpec(vRefNum, dirID, fileName, &fsspec)
        // Trap: _HFSDispatch (0xA260) with D0 selector = 0x001B
        // Param block at ScratchMem + 0xC00:
        //   +18 ioNamePtr: pointer to Pascal string
        //   +22 ioVRefNum: 0 (resolve from path)
        //   +48 ioDirID: 0
        // FSSpec output at ScratchMem + 0xD00:
        //   +0 vRefNum (int16)
        //   +2 parID (uint32)
        //   +6 name (Str63 = Pascal string)
        uint32_t pb_addr = SCRATCH_BASE + 0xC00;
        uint32_t fsspec_addr = SCRATCH_BASE + 0xD00;

        // Zero param block (108 bytes)
        for (int i = 0; i < 108; i++)
            WriteMacInt8(pb_addr + i, 0);
        WriteMacInt32(pb_addr + 18, MB_CMD_ARG);   // ioNamePtr
        WriteMacInt16(pb_addr + 22, 0);             // ioVRefNum
        WriteMacInt32(pb_addr + 48, 0);             // ioDirID

        M68kRegisters fs_regs;
        memset(&fs_regs, 0, sizeof(fs_regs));
        fs_regs.a[0] = pb_addr;
        fs_regs.d[0] = 0x001B;  // FSMakeFSSpec selector
        Execute68kTrap(0xA260, &fs_regs);  // _HFSDispatch

        int16_t fs_err = (int16_t)ReadMacInt16(pb_addr + 16);  // ioResult
        if (fs_err != 0 && fs_err != -43) {
            // -43 = fnfErr is OK (FSMakeFSSpec can return it for valid paths)
            fprintf(stderr, "[CommandBridge] FSMakeFSSpec failed: %d\n", fs_err);
            WriteMacInt16(MB_RESULT_ERR, fs_err);
            WriteMacInt8(MB_RESULT_FLAG, 1);
            break;
        }

        // Copy FSSpec from param block output
        // PBMakeFSSpec stores the FSSpec fields in the param block:
        //   +22 ioVRefNum -> fsspec.vRefNum
        //   +48 ioDirID   -> fsspec.parID
        //   +18 ioNamePtr -> name is already the Pascal string
        // But we need to build a proper FSSpec struct at fsspec_addr
        int16_t vRefNum = (int16_t)ReadMacInt16(pb_addr + 22);
        uint32_t parID = ReadMacInt32(pb_addr + 48);

        // Build FSSpec: vRefNum(2) + parID(4) + name(64)
        WriteMacInt16(fsspec_addr + 0, vRefNum);
        WriteMacInt32(fsspec_addr + 2, parID);
        // Copy the filename part (last component) from the path
        // After FSMakeFSSpec, ioNamePtr points to the resolved name
        // We need to extract just the filename from the full path
        // The param block's ioNamePtr still points to our full path,
        // but ioDirID and ioVRefNum are resolved.
        // Copy the leaf name: find last ':' in the path
        int leaf_start = 0;
        for (int i = namelen - 1; i >= 0; i--) {
            if (name[i] == ':') { leaf_start = i + 1; break; }
        }
        uint8_t leaf_len = namelen - leaf_start;
        if (leaf_len > 63) leaf_len = 63;
        WriteMacInt8(fsspec_addr + 6, leaf_len);
        for (int i = 0; i < leaf_len; i++)
            WriteMacInt8(fsspec_addr + 7 + i, (uint8_t)name[leaf_start + i]);

        fprintf(stderr, "[CommandBridge] FSSpec: vRefNum=%d, parID=%u, name='%.*s'\n",
                vRefNum, parID, leaf_len, name + leaf_start);

        // --- Step 2: Build extended LaunchParamBlockRec ---
        // At ScratchMem + 0xE00:
        //   +0  reserved1       (uint32) = 0
        //   +4  reserved2       (uint16) = 0
        //   +6  launchBlockID   (uint16) = 'LC' = 0x4C43 (extendedBlock)
        //   +8  launchEPBLength (uint32) = 6 (extendedBlockLen)
        //   +12 launchFileFlags (uint16) = 0
        //   +14 launchControlFlags (uint16) = 0xC000 (launchContinue | launchNoFileFlags)
        //   +16 launchAppSpec   (uint32) = pointer to FSSpec
        uint32_t lpb_addr = SCRATCH_BASE + 0xE00;
        for (int i = 0; i < 32; i++)
            WriteMacInt8(lpb_addr + i, 0);
        WriteMacInt16(lpb_addr + 6, 0x4C43);       // launchBlockID = extendedBlock
        WriteMacInt32(lpb_addr + 8, 6);             // launchEPBLength
        WriteMacInt16(lpb_addr + 12, 0);            // launchFileFlags
        WriteMacInt16(lpb_addr + 14, 0xC000);       // launchContinue | launchNoFileFlags
        WriteMacInt32(lpb_addr + 16, fsspec_addr);  // launchAppSpec

        // --- Step 3: Call _Launch with extended param block ---
        // For the extended form, the param block pointer goes on the stack
        // Actually, _LaunchApplication uses register A0 = pointer to LaunchParamBlockRec
        // (it's a register-based OS trap)
        WriteMacInt16(MB_RESULT_ERR, 0);
        WriteMacInt8(MB_RESULT_FLAG, 1);  // optimistic — write before launch

        M68kRegisters launch_regs;
        memset(&launch_regs, 0, sizeof(launch_regs));
        launch_regs.a[0] = lpb_addr;
        Execute68kTrap(0xA9F2, &launch_regs);  // _Launch

        // Extended _Launch with launchContinue should return
        int16_t launch_err = (int16_t)(launch_regs.d[0] & 0xFFFF);
        fprintf(stderr, "[CommandBridge] _Launch returned: d0=%d\n", launch_err);
        if (launch_err != 0) {
            WriteMacInt16(MB_RESULT_ERR, launch_err);
        }
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
            name[i] = (char)ReadMacInt8(0x0911 + i);
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
                    if (tlen > 0 && tlen < 256) {
                        for (int i = 0; i < tlen; i++) {
                            char c = (char)ReadMacInt8(title_ptr + 1 + i);
                            if (c == '"' || c == '\\') json += "\\";
                            title += c;
                        }
                    }
                }
            }

            int16_t top = (int16_t)ReadMacInt16(wp + 16);
            int16_t left = (int16_t)ReadMacInt16(wp + 18);
            int16_t bottom = (int16_t)ReadMacInt16(wp + 20);
            int16_t right = (int16_t)ReadMacInt16(wp + 22);
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

void CommandBridge::execute_one_public(const Command& cmd, M68kRegisters* r, CommandResult& result) {
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
        uint8_t plen = (uint8_t)std::min((int)path.size(), 255);

        // Validate file exists using _GetFileInfo (trap 0xA00C)
        uint32_t name_addr = SCRATCH_BASE;
        WriteMacInt8(name_addr, plen);
        for (int i = 0; i < plen; i++)
            WriteMacInt8(name_addr + 1 + i, (uint8_t)path[i]);

        uint32_t pb_addr = SCRATCH_BASE + 512;
        for (int i = 0; i < 80; i++)
            WriteMacInt8(pb_addr + i, 0);
        WriteMacInt32(pb_addr + 18, name_addr);
        WriteMacInt16(pb_addr + 22, 0);
        WriteMacInt16(pb_addr + 28, 0);

        M68kRegisters check_regs;
        memset(&check_regs, 0, sizeof(check_regs));
        check_regs.a[0] = pb_addr;
        Execute68kTrap(0xA00C, &check_regs);

        int16_t file_err = (int16_t)ReadMacInt16(pb_addr + 16);
        if (file_err != 0) {
            result.err = file_err;
            char buf[128];
            snprintf(buf, sizeof(buf), "File not found (error %d)", file_err);
            result.data = buf;
            fprintf(stderr, "[CommandBridge] Launch validation failed: %s -> error %d\n",
                    path.c_str(), file_err);
            break;
        }

        // Write path to mailbox and set command flag.
        // The jGNEFilter will pick it up on the next event loop iteration
        // and execute _Launch in application context.
        WriteMacInt8(MB_RESULT_FLAG, 0);
        WriteMacInt16(MB_CMD_TYPE, MB_CMD_LAUNCH);
        WriteMacInt8(MB_CMD_ARG, plen);
        for (int i = 0; i < plen; i++)
            WriteMacInt8(MB_CMD_ARG + 1 + i, (uint8_t)path[i]);
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
// SHM queue drain (child side, called from 60Hz IRQ)
// ============================================================================

static CmdType shm_type_to_cmd(int32_t type) {
    switch (type) {
    case SharedState::CMD_GET_APP_NAME:    return CmdType::GET_APP_NAME;
    case SharedState::CMD_GET_WINDOW_LIST: return CmdType::GET_WINDOW_LIST;
    case SharedState::CMD_GET_TICKS:       return CmdType::GET_TICKS;
    case SharedState::CMD_READ_MEMORY:     return CmdType::READ_MEMORY;
    case SharedState::CMD_LAUNCH_APP:      return CmdType::LAUNCH_APP;
    case SharedState::CMD_QUIT_APP:        return CmdType::QUIT_APP;
    default:                               return CmdType::GET_APP_NAME;
    }
}

static void drain_shm_commands(SharedState* shm, M68kRegisters* r) {
    int32_t read_pos = shm->cmd_read_pos.load(std::memory_order_relaxed);
    int32_t write_pos = shm->cmd_write_pos.load(std::memory_order_acquire);

    while (read_pos != write_pos) {
        auto& cmd = shm->cmd_queue[read_pos & (SharedState::CMD_QUEUE_SIZE - 1)];

        Command internal_cmd;
        internal_cmd.id = cmd.id;
        internal_cmd.type = shm_type_to_cmd(cmd.type);
        internal_cmd.arg = cmd.arg;
        internal_cmd.addr = cmd.addr;
        internal_cmd.len = cmd.len;

        // Advance read_pos BEFORE executing — Execute68kTrap may re-enter IRQ
        read_pos++;
        shm->cmd_read_pos.store(read_pos, std::memory_order_release);

        CommandResult result;
        result.id = internal_cmd.id;
        result.done = true;
        g_command_bridge.execute_one_public(internal_cmd, r, result);

        // Write result to shared memory
        int32_t res_pos = shm->result_write_pos.load(std::memory_order_relaxed);
        auto& res = shm->result_queue[res_pos & (SharedState::CMD_QUEUE_SIZE - 1)];
        res.id = result.id;
        res.done = 1;
        res.err = result.err;
        strncpy(res.data, result.data.c_str(), SharedState::CMD_DATA_SIZE - 1);
        res.data[SharedState::CMD_DATA_SIZE - 1] = '\0';
        shm->result_write_pos.store(res_pos + 1, std::memory_order_release);

        // Re-read write_pos in case new commands were queued during execution
        write_pos = shm->cmd_write_pos.load(std::memory_order_acquire);
    }
}

static void update_shm_passive(SharedState* shm) {
    if (!RAMBaseHost || RAMSize == 0) return;

    uint8_t namelen = ReadMacInt8(0x0910);
    if (namelen > 31) namelen = 31;
    char name[32];
    for (int i = 0; i < namelen; i++)
        name[i] = (char)ReadMacInt8(0x0911 + i);
    name[namelen] = '\0';
    memcpy(shm->cur_app_name, name, 32);
}

// ============================================================================
// Entry point from emul_op.cpp IRQ handler
// ============================================================================

void command_bridge_drain_from_irq(M68kRegisters* r) {
    // Install jGNEFilter once (first IRQ after Mac has started)
    if (!g_filter_installed) {
        install_jgne_filter();
    }

    // Drain shared memory command queue (fork mode)
    SharedState* shm = g_child_shared_state;
    if (shm) {
        drain_shm_commands(shm, r);
        update_shm_passive(shm);
    }

    // Drain in-process queue (legacy)
    g_command_bridge.drain(r);
}

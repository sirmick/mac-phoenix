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
static std::atomic<bool> g_filter_installed{false};

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
    if (g_filter_installed.load(std::memory_order_acquire)) return;
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
            name[i] = static_cast<char>(ReadMacInt8(MB_CMD_ARG + 1 + i));
        name[namelen] = '\0';
        fprintf(stderr, "[CommandBridge] INIT launching: %s (NEW CODE PATH)\n", name);
        fflush(stderr);

        // --- Step 1: Resolve path via PBGetCatInfo + PBHGetVInfo ---
        // Strategy: split "Volume:File" path manually. Use PBHGetVInfo to iterate
        // volumes and find the one with the matching name — this gives us the
        // correct vRefNum. Then call PBGetCatInfo on "File" with that vRefNum
        // to get parID.
        //
        // CInfoPBRec layout (same as HParamBlockRec for volume queries):
        //   +16 ioResult (int16)
        //   +18 ioNamePtr (void*)
        //   +22 ioVRefNum (int16)
        //   +28 ioVolIndex (int16) for PBHGetVInfo
        //   +100 ioFlParID (uint32) for PBGetCatInfo
        uint32_t pb_addr = SCRATCH_BASE + 0xC00;
        uint32_t fsspec_addr = SCRATCH_BASE + 0xD00;
        uint32_t vol_name_addr = SCRATCH_BASE + 0xE80;  // scratch for volume name lookup

        // Split path at first ':'
        int colon_pos = -1;
        for (int i = 0; i < namelen; i++) {
            if (name[i] == ':') { colon_pos = i; break; }
        }
        fprintf(stderr, "[CommandBridge] parsing path: namelen=%d colon_pos=%d\n",
                (int)namelen, colon_pos);
        fflush(stderr);

        int16_t vRefNum = 0;
        uint32_t parID = 2;  // HFS root dir ID
        int leaf_start = 0;

        if (colon_pos >= 0) {
            // Path has "Volume:..." — find the volume
            char volname[64];
            int vnlen = colon_pos;
            if (vnlen > 63) vnlen = 63;
            for (int i = 0; i < vnlen; i++) volname[i] = name[i];
            volname[vnlen] = '\0';

            // Iterate volumes via PBHGetVInfo(ioVolIndex=1,2,...)
            bool found = false;
            for (int16_t idx = 1; idx < 16 && !found; idx++) {
                for (int i = 0; i < 108; i++) WriteMacInt8(pb_addr + i, 0);
                // Write a Pascal string buffer for the returned name
                WriteMacInt8(vol_name_addr, 0);  // length 0 initially
                WriteMacInt32(pb_addr + 18, vol_name_addr);  // ioNamePtr
                WriteMacInt16(pb_addr + 22, 0);               // ioVRefNum
                WriteMacInt16(pb_addr + 28, idx);             // ioVolIndex

                M68kRegisters vi_regs;
                memset(&vi_regs, 0, sizeof(vi_regs));
                vi_regs.a[0] = pb_addr;
                Execute68kTrap(0xA207, &vi_regs);  // _HGetVInfo

                int16_t vi_err = static_cast<int16_t>(ReadMacInt16(pb_addr + 16));
                if (vi_err != 0) {
                    fprintf(stderr, "[CommandBridge] PBHGetVInfo idx=%d err=%d\n", idx, vi_err);
                    break;
                }

                uint8_t rvnlen = ReadMacInt8(vol_name_addr);
                char rvn[64];
                for (int i = 0; i < rvnlen && i < 63; i++)
                    rvn[i] = static_cast<char>(ReadMacInt8(vol_name_addr + 1 + i));
                rvn[rvnlen] = '\0';
                int16_t this_vref = static_cast<int16_t>(ReadMacInt16(pb_addr + 22));
                fprintf(stderr, "[CommandBridge] Vol idx=%d vRefNum=%d name='%s'\n",
                        idx, this_vref, rvn);

                if (rvnlen == vnlen && memcmp(rvn, volname, vnlen) == 0) {
                    vRefNum = this_vref;
                    found = true;
                }
            }

            if (!found) {
                fprintf(stderr, "[CommandBridge] Volume '%s' not found\n", volname);
                WriteMacInt16(MB_RESULT_ERR, -35);  // nsvErr
                WriteMacInt8(MB_RESULT_FLAG, 1);
                break;
            }
            leaf_start = colon_pos + 1;
        }

        // For a file at volume root, parID=2 is the HFS root dir.
        // (We could call PBGetCatInfo to confirm, but it was unreliable here.)
        uint8_t leaf_len = namelen - leaf_start;
        if (leaf_len > 63) leaf_len = 63;
        parID = 2;

        // Build FSSpec: vRefNum(2) + parID(4) + name(64)
        // leaf_len/leaf_start were set above during leaf name construction.
        WriteMacInt16(fsspec_addr + 0, vRefNum);
        WriteMacInt32(fsspec_addr + 2, parID);
        WriteMacInt8(fsspec_addr + 6, leaf_len);
        for (int i = 0; i < leaf_len; i++)
            WriteMacInt8(fsspec_addr + 7 + i, static_cast<uint8_t>(name[leaf_start + i]));

        fprintf(stderr, "[CommandBridge] FSSpec: vRefNum=%d, parID=%u, name='%.*s'\n",
                vRefNum, parID, leaf_len, name + leaf_start);
        fflush(stderr);
        { const char *msg = "[raw] point-A after FSSpec print\n"; (void)!write(2, msg, strlen(msg)); fsync(2); }

        // --- Sanity check: can we actually open this file via FSSpec? ---
        // PBHOpenDFSync via _HOpenDF (0xA200) with ioNamePtr=leaf and ioVRefNum=vRefNum
        {
            uint32_t open_pb = SCRATCH_BASE + 0xF00;
            uint32_t leaf_pstr = SCRATCH_BASE + 0xFA0;
            // Write leaf as Pascal string
            WriteMacInt8(leaf_pstr, leaf_len);
            for (int i = 0; i < leaf_len; i++)
                WriteMacInt8(leaf_pstr + 1 + i, static_cast<uint8_t>(name[leaf_start + i]));

            for (int i = 0; i < 80; i++) WriteMacInt8(open_pb + i, 0);
            WriteMacInt32(open_pb + 18, leaf_pstr);       // ioNamePtr
            WriteMacInt16(open_pb + 22, vRefNum);          // ioVRefNum
            WriteMacInt8(open_pb + 27, 0x01);              // ioPermssn = fsRdPerm

            M68kRegisters open_regs;
            memset(&open_regs, 0, sizeof(open_regs));
            open_regs.a[0] = open_pb;
            Execute68kTrap(0xA200, &open_regs);  // _HOpen / _PBHOpen (data fork)
            int16_t open_err = static_cast<int16_t>(ReadMacInt16(open_pb + 16));
            int16_t open_refnum = static_cast<int16_t>(ReadMacInt16(open_pb + 24));
            fprintf(stderr, "[CommandBridge] PBHOpen DF: err=%d refNum=%d\n",
                    open_err, open_refnum);
            fflush(stderr);
            if (open_err == 0) {
                // Close it again
                for (int i = 0; i < 80; i++) WriteMacInt8(open_pb + i, 0);
                WriteMacInt16(open_pb + 24, open_refnum);
                M68kRegisters close_regs;
                memset(&close_regs, 0, sizeof(close_regs));
                close_regs.a[0] = open_pb;
                Execute68kTrap(0xA001, &close_regs);  // _Close
            }

            // Also try _HOpenRF (0xA20A) for resource fork
            for (int i = 0; i < 80; i++) WriteMacInt8(open_pb + i, 0);
            WriteMacInt32(open_pb + 18, leaf_pstr);
            WriteMacInt16(open_pb + 22, vRefNum);
            WriteMacInt8(open_pb + 27, 0x01);
            M68kRegisters rf_regs;
            memset(&rf_regs, 0, sizeof(rf_regs));
            rf_regs.a[0] = open_pb;
            Execute68kTrap(0xA20A, &rf_regs);  // _HOpenRF
            int16_t rf_err = static_cast<int16_t>(ReadMacInt16(open_pb + 16));
            int16_t rf_refnum = static_cast<int16_t>(ReadMacInt16(open_pb + 24));
            fprintf(stderr, "[CommandBridge] PBHOpen RF: err=%d refNum=%d\n",
                    rf_err, rf_refnum);
            fflush(stderr);
            if (rf_err == 0) {
                for (int i = 0; i < 80; i++) WriteMacInt8(open_pb + i, 0);
                WriteMacInt16(open_pb + 24, rf_refnum);
                M68kRegisters close_regs;
                memset(&close_regs, 0, sizeof(close_regs));
                close_regs.a[0] = open_pb;
                Execute68kTrap(0xA001, &close_regs);
            }
        }

        // --- Step 2: Build extended LaunchParamBlockRec ---
        // At ScratchMem + 0xE00:
        // LaunchParamBlockRec (Inside Macintosh: Processes):
        //   +0  reserved1          (LongInt) = 0
        //   +4  reserved2          (Word)    = 0
        //   +6  launchBlockID      (Word)    = 'LC' = 0x4C43 (extendedBlock)
        //   +8  launchEPBLength    (LongInt) = 6 (extendedBlockLen)
        //   +12 launchFileFlags    (Word)    = 0
        //   +14 launchControlFlags (LongInt) = 0x4000 (launchContinue)
        //   +18 launchAppSpec      (Ptr)     = pointer to FSSpec
        //   +22 launchAppParameters (LongInt) = 0
        uint32_t lpb_addr = SCRATCH_BASE + 0xE00;
        for (int i = 0; i < 32; i++)
            WriteMacInt8(lpb_addr + i, 0);
        WriteMacInt16(lpb_addr + 6, 0x4C43);         // launchBlockID = extendedBlock ('LC')
        WriteMacInt32(lpb_addr + 8, 6);               // launchEPBLength
        WriteMacInt16(lpb_addr + 12, 0);              // launchFileFlags
        WriteMacInt32(lpb_addr + 14, 0x4000);         // launchControlFlags = launchContinue
        WriteMacInt32(lpb_addr + 18, fsspec_addr);    // launchAppSpec
        WriteMacInt32(lpb_addr + 22, 0);              // launchAppParameters

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
        { const char *m = "[raw] MB_CMD_LAUNCH END\n"; (void)!write(2, m, strlen(m)); fsync(2); }
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

        // Validate file exists using _GetFileInfo (trap 0xA00C)
        uint32_t name_addr = SCRATCH_BASE;
        WriteMacInt8(name_addr, plen);
        for (int i = 0; i < plen; i++)
            WriteMacInt8(name_addr + 1 + i, static_cast<uint8_t>(path[i]));

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

        int16_t file_err = static_cast<int16_t>(ReadMacInt16(pb_addr + 16));
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

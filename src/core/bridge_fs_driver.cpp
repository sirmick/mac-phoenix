/*
 * bridge_fs_driver.cpp - In-memory "Bridge:" Mac filesystem driver
 *
 * Registers a standalone Mac filesystem via the File System Manager.
 * All data is stored in the BridgeFS in-memory store (bridge_fs.h).
 * No host filesystem involvement — writes are immediately visible.
 *
 * The registration follows the same FSM protocol as ExtFS (extfs.cpp)
 * but the dispatch handlers are much simpler: flat file namespace,
 * no directories, no resource forks.
 */

#include "bridge_fs_driver.h"
#include "bridge_fs.h"
#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/m68k_registers.h"
#include "../common/include/macos_util.h"
#include "../common/include/extfs_defs.h"
#include "../common/include/emul_op.h"
#include "../common/include/platform.h"
#include "../config/emulator_config.h"
#include "../common/include/disk.h"
#include <cstdio>
#include <cstring>
#include <string>

// ============================================================================
// Constants
// ============================================================================

static const int16_t  BRIDGE_FSID       = 0x6272;      // 'br'
static const uint32_t BRIDGE_MEDIA_TYPE = 0x62726467;   // 'brdg'
static const int      STACK_SIZE        = 0x10000;

// Volume and FS names (Pascal strings)
static char BRIDGE_VOLUME_NAME[32];  // initialized in InstallBridgeFS
static char BRIDGE_FS_NAME[32];

// fs_data layout — same offsets as ExtFS (802 bytes)
enum {
    bfsCommProcStub = 0,
    bfsHFSProcStub  = 6,
    bfsDrvStatus    = 12,
    bfsFSD          = 42,
    bfsPB           = 238,
    bfsVMI          = 288,
    bfsReturn       = 306,
    bfsAllocateVCB  = 562,
    bfsAddNewVCB    = 578,
    bfsDetermineVol = 594,
    bfsAllocateFCB  = 710,
    bfsReleaseFCB   = 724,
    bfsResolveFCB   = 752,
    bfsAdjustEOF    = 766,
    SIZEOF_bfsdat   = 802
};

// FSD offsets — reuse from extfs_defs.h (already included)

static uint32_t bfs_data = 0;   // Mac address of our data block
static int16_t  drive_number = 0;

// ============================================================================
// Helper: read Pascal string from Mac memory
// ============================================================================

static std::string read_pstring(uint32_t addr) {
    if (addr == 0) return "";
    uint8_t len = ReadMacInt8(addr);
    std::string s;
    for (int i = 0; i < len; i++)
        s += static_cast<char>(ReadMacInt8(addr + 1 + i));
    return s;
}

// ============================================================================
// Communications proc
// ============================================================================

int16_t BridgeFSComm(uint16_t message, uint32_t paramBlock, uint32_t globals) {
    (void)globals;
    static int comm_count = 0;
    if (++comm_count <= 5)
        fprintf(stderr, "[BridgeFS] Comm: msg=%d pb=0x%08X\n", message, paramBlock);
    switch (message) {
        case 0:  // ffsNopMessage
        case 1:  // ffsLoadMessage
        case 2:  // ffsUnloadMessage
            return 0;
        case 4:  // ffsIDDiskMessage
            return ((int16_t)ReadMacInt16(paramBlock + ioVRefNum) == drive_number) ? 0 : -1;
        case 5:  // ffsIDVolMountMessage
            return (ReadMacInt32(ReadMacInt32(paramBlock + ioBuffer) + 4) == BRIDGE_MEDIA_TYPE) ? 0 : -1;
        default:
            return -1;
    }
}

// ============================================================================
// HFS dispatch — file operations backed by BridgeFS in-memory store
// ============================================================================

// FCB field offsets (from extfs_defs.h — already defined, just use them)

static int16_t bridge_open(uint32_t pb, uint32_t vcb) {
    uint32_t name_ptr = ReadMacInt32(pb + ioNamePtr);
    std::string name = read_pstring(name_ptr);
    if (name.empty()) return -43; // fnfErr

    uint8_t perm = ReadMacInt8(pb + ioPermssn);
    bool writable = (perm != 1); // fsRdPerm = 1

    if (!g_bridge_fs) return -43;
    int fd = g_bridge_fs->open(name, writable);
    if (fd < 0) return -43; // fnfErr

    // Allocate FCB via Mac utility
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.a[0] = bfs_data + bfsReturn;
    r.a[1] = bfs_data + bfsReturn + 2;
    Execute68k(bfs_data + bfsAllocateFCB, &r);
    if (r.d[0] & 0xffff) {
        g_bridge_fs->close(fd);
        return static_cast<int16_t>(r.d[0]);
    }

    int16_t refNum = static_cast<int16_t>(ReadMacInt16(bfs_data + bfsReturn));
    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn + 2);

    int64_t file_size = g_bridge_fs->get_eof(fd);

    // Set up FCB
    WriteMacInt32(fcb + fcbFlNm, (uint32_t)(fd + 1));  // use fd+1 as "file number"
    WriteMacInt8(fcb + fcbFlags, writable ? (1 << 0) : 0); // fcbResourceMask=0, write flag
    WriteMacInt32(fcb + fcbEOF, (uint32_t)file_size);
    WriteMacInt32(fcb + fcbPLen, (uint32_t)((file_size | 0x1FF) + 1));
    WriteMacInt32(fcb + fcbCrPs, 0);
    WriteMacInt32(fcb + fcbVPtr, vcb);

    WriteMacInt16(pb + ioRefNum, refNum);
    return 0;
}

static int16_t bridge_close(uint32_t pb) {
    int16_t refNum = static_cast<int16_t>(ReadMacInt16(pb + ioRefNum));

    // Resolve FCB
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    r.a[0] = bfs_data + bfsReturn;
    Execute68k(bfs_data + bfsResolveFCB, &r);
    if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);

    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn);
    int fd = static_cast<int>(ReadMacInt32(fcb + fcbFlNm)) - 1;

    if (g_bridge_fs && fd >= 0) g_bridge_fs->close(fd);

    // Release FCB
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    Execute68k(bfs_data + bfsReleaseFCB, &r);
    return 0;
}

static int16_t bridge_read(uint32_t pb) {
    int16_t refNum = static_cast<int16_t>(ReadMacInt16(pb + ioRefNum));

    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    r.a[0] = bfs_data + bfsReturn;
    Execute68k(bfs_data + bfsResolveFCB, &r);
    if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);

    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn);
    int fd = static_cast<int>(ReadMacInt32(fcb + fcbFlNm)) - 1;
    if (!g_bridge_fs || fd < 0) return -51; // rfNumErr

    // Handle position modes
    uint16_t posMode = ReadMacInt16(pb + ioPosMode) & 3;
    int32_t posOffset = static_cast<int32_t>(ReadMacInt32(pb + ioPosOffset));
    switch (posMode) {
        case 1: g_bridge_fs->seek(fd, posOffset, 0); break;     // fsFromStart
        case 2: g_bridge_fs->seek(fd, posOffset, 2); break;     // fsFromLEOF
        case 3: g_bridge_fs->seek(fd, posOffset, 1); break;     // fsFromMark
    }

    uint32_t reqCount = ReadMacInt32(pb + ioReqCount);
    uint8_t *buf = Mac2HostAddr(ReadMacInt32(pb + ioBuffer));
    ssize_t actual = g_bridge_fs->read(fd, buf, reqCount);

    WriteMacInt32(pb + ioActCount, actual > 0 ? actual : 0);

    // Update FCB position
    int64_t pos = g_bridge_fs->seek(fd, 0, 1); // get current pos
    WriteMacInt32(fcb + fcbCrPs, (uint32_t)pos);
    WriteMacInt32(pb + ioPosOffset, (uint32_t)pos);

    if (actual == 0 && reqCount > 0) return -39; // eofErr
    return 0;
}

static int16_t bridge_write(uint32_t pb) {
    int16_t refNum = static_cast<int16_t>(ReadMacInt16(pb + ioRefNum));

    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    r.a[0] = bfs_data + bfsReturn;
    Execute68k(bfs_data + bfsResolveFCB, &r);
    if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);

    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn);
    int fd = static_cast<int>(ReadMacInt32(fcb + fcbFlNm)) - 1;
    if (!g_bridge_fs || fd < 0) return -51;

    uint16_t posMode = ReadMacInt16(pb + ioPosMode) & 3;
    int32_t posOffset = static_cast<int32_t>(ReadMacInt32(pb + ioPosOffset));
    switch (posMode) {
        case 1: g_bridge_fs->seek(fd, posOffset, 0); break;
        case 2: g_bridge_fs->seek(fd, posOffset, 2); break;
        case 3: g_bridge_fs->seek(fd, posOffset, 1); break;
    }

    uint32_t reqCount = ReadMacInt32(pb + ioReqCount);
    uint8_t *buf = Mac2HostAddr(ReadMacInt32(pb + ioBuffer));
    ssize_t actual = g_bridge_fs->write(fd, buf, reqCount);

    WriteMacInt32(pb + ioActCount, actual > 0 ? actual : 0);

    int64_t pos = g_bridge_fs->seek(fd, 0, 1);
    int64_t eof = g_bridge_fs->get_eof(fd);
    WriteMacInt32(fcb + fcbCrPs, (uint32_t)pos);
    WriteMacInt32(fcb + fcbEOF, (uint32_t)eof);
    WriteMacInt32(fcb + fcbPLen, (uint32_t)((eof | 0x1FF) + 1));
    WriteMacInt32(pb + ioPosOffset, (uint32_t)pos);

    return 0;
}

static int16_t bridge_create(uint32_t pb) {
    uint32_t name_ptr = ReadMacInt32(pb + ioNamePtr);
    std::string name = read_pstring(name_ptr);
    if (name.empty()) return -37; // bdNamErr

    if (!g_bridge_fs) return -50; // paramErr
    if (g_bridge_fs->exists(name)) return -48; // dupFNErr
    g_bridge_fs->create(name);
    return 0;
}

static int16_t bridge_delete(uint32_t pb) {
    uint32_t name_ptr = ReadMacInt32(pb + ioNamePtr);
    std::string name = read_pstring(name_ptr);
    if (name.empty()) return -37;

    if (!g_bridge_fs) return -50;
    if (!g_bridge_fs->exists(name)) return -43; // fnfErr
    g_bridge_fs->remove(name);
    return 0;
}

static int16_t bridge_get_file_info(uint32_t pb) {
    uint32_t name_ptr = ReadMacInt32(pb + ioNamePtr);
    std::string name = read_pstring(name_ptr);
    if (name.empty()) return -43;

    if (!g_bridge_fs || !g_bridge_fs->exists(name)) return -43;

    // Fill in minimal file info
    std::string data;
    g_bridge_fs->get_file(name, data);

    // ioFlFndrInfo at pb+32 (FInfo: 16 bytes)
    WriteMacInt32(pb + 32, 0x54455854); // fdType = 'TEXT'
    WriteMacInt32(pb + 36, 0x74747874); // fdCreator = 'ttxt'
    WriteMacInt16(pb + 40, 0);          // fdFlags
    WriteMacInt32(pb + 42, 0);          // fdLocation
    WriteMacInt16(pb + 46, 0);          // fdFldr

    // Logical/physical size
    WriteMacInt32(pb + 54, (uint32_t)data.size()); // ioFlLgLen (data fork logical)
    WriteMacInt32(pb + 58, (uint32_t)((data.size() | 0x1FF) + 1)); // ioFlPyLen
    WriteMacInt32(pb + 64, 0); // ioFlRLgLen (resource fork = 0)
    WriteMacInt32(pb + 68, 0); // ioFlRPyLen

    WriteMacInt8(pb + 30, 0);  // ioFlAttrib (not locked, not directory)
    return 0;
}

static int16_t bridge_get_vol_info(uint32_t pb, uint32_t vcb) {
    // Copy volume name from VCB
    uint32_t name_ptr = ReadMacInt32(pb + ioNamePtr);
    if (name_ptr) {
        // Copy volume name from VCB to param block (both Mac addresses)
        for (int i = 0; i < 28; i++)
            WriteMacInt8(name_ptr + i, ReadMacInt8(vcb + vcbVN + i));
    }
    return 0;
}

static int16_t bridge_get_eof(uint32_t pb) {
    int16_t refNum = static_cast<int16_t>(ReadMacInt16(pb + ioRefNum));
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    r.a[0] = bfs_data + bfsReturn;
    Execute68k(bfs_data + bfsResolveFCB, &r);
    if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);

    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn);
    int fd = static_cast<int>(ReadMacInt32(fcb + fcbFlNm)) - 1;
    if (!g_bridge_fs || fd < 0) return -51;

    int64_t eof = g_bridge_fs->get_eof(fd);
    WriteMacInt32(pb + ioMisc, (uint32_t)eof);
    return 0;
}

static int16_t bridge_set_eof(uint32_t pb) {
    int16_t refNum = static_cast<int16_t>(ReadMacInt16(pb + ioRefNum));
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    r.a[0] = bfs_data + bfsReturn;
    Execute68k(bfs_data + bfsResolveFCB, &r);
    if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);

    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn);
    int fd = static_cast<int>(ReadMacInt32(fcb + fcbFlNm)) - 1;
    if (!g_bridge_fs || fd < 0) return -51;

    int64_t size = ReadMacInt32(pb + ioMisc);
    g_bridge_fs->set_eof(fd, size);
    WriteMacInt32(fcb + fcbEOF, (uint32_t)size);
    WriteMacInt32(fcb + fcbPLen, (uint32_t)((size | 0x1FF) + 1));
    return 0;
}

static int16_t bridge_set_fpos(uint32_t pb) {
    int16_t refNum = static_cast<int16_t>(ReadMacInt16(pb + ioRefNum));
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.d[0] = refNum;
    r.a[0] = bfs_data + bfsReturn;
    Execute68k(bfs_data + bfsResolveFCB, &r);
    if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);

    uint32_t fcb = ReadMacInt32(bfs_data + bfsReturn);
    int fd = static_cast<int>(ReadMacInt32(fcb + fcbFlNm)) - 1;
    if (!g_bridge_fs || fd < 0) return -51;

    uint16_t posMode = ReadMacInt16(pb + ioPosMode) & 3;
    int32_t posOffset = static_cast<int32_t>(ReadMacInt32(pb + ioPosOffset));
    int whence = 0;
    switch (posMode) {
        case 1: whence = 0; break; // fsFromStart
        case 2: whence = 2; break; // fsFromLEOF
        case 3: whence = 1; break; // fsFromMark
    }
    int64_t pos = g_bridge_fs->seek(fd, posOffset, whence);
    WriteMacInt32(fcb + fcbCrPs, (uint32_t)pos);
    WriteMacInt32(pb + ioPosOffset, (uint32_t)pos);
    return 0;
}

// ============================================================================
// Main HFS dispatch
// ============================================================================

int16_t BridgeFSHFS(uint32_t vcb, uint16_t selectCode, uint32_t paramBlock,
                     uint32_t globals, int16_t fsid) {
    (void)globals; (void)fsid;
    static int call_count = 0;
    if (++call_count <= 20)
        fprintf(stderr, "[BridgeFS] HFS dispatch: sel=0x%04X pb=0x%08X\n", selectCode, paramBlock);

    switch (selectCode) {
        case 0xA000: return bridge_open(paramBlock, vcb);     // kFSMOpen
        case 0xA001: return bridge_close(paramBlock);          // kFSMClose
        case 0xA002: return bridge_read(paramBlock);           // kFSMRead
        case 0xA003: return bridge_write(paramBlock);          // kFSMWrite
        case 0xA008: return bridge_create(paramBlock);         // kFSMCreate
        case 0xA009: return bridge_delete(paramBlock);         // kFSMDelete
        case 0xA00C: return bridge_get_file_info(paramBlock);  // kFSMGetFileInfo
        case 0xA00D: return 0;                                 // kFSMSetFileInfo (no-op)
        case 0xA007: return bridge_get_vol_info(paramBlock, vcb); // kFSMGetVolInfo
        case 0xA010: return bridge_get_eof(paramBlock);        // kFSMGetEOF
        case 0xA011: return bridge_set_eof(paramBlock);        // kFSMSetEOF
        case 0xA044: return bridge_set_fpos(paramBlock);       // kFSMSetFPos

        // Flush — no-op for in-memory store
        case 0xA013: // kFSMFlushVol
        case 0xA014: // kFSMFlushFile
            return 0;

        // Volume mount — allocate VCB, init, add to queue (same as ExtFS)
        case 0x0041: {
            M68kRegisters r;

            // Allocate VCB
            memset(&r, 0, sizeof(r));
            r.a[0] = bfs_data + bfsReturn;
            r.a[1] = bfs_data + bfsReturn + 2;
            Execute68k(bfs_data + bfsAllocateVCB, &r);
            if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);
            uint32_t new_vcb = ReadMacInt32(bfs_data + bfsReturn + 2);

            // Init VCB
            WriteMacInt16(new_vcb + vcbSigWord, 0x4244);
            WriteMacInt32(new_vcb + vcbCrDate, 0);
            WriteMacInt32(new_vcb + vcbLsMod, 0);
            WriteMacInt16(new_vcb + vcbNmFls, 0);
            WriteMacInt16(new_vcb + vcbNmRtDirs, 0);
            WriteMacInt16(new_vcb + vcbNmAlBlks, 0xFFFF);
            WriteMacInt32(new_vcb + vcbAlBlkSiz, 0x4000);
            WriteMacInt32(new_vcb + vcbClpSiz, 0x4000);
            WriteMacInt16(new_vcb + vcbFreeBks, 0xFFFF);
            for (int i = 0; i < 28; i++)
                WriteMacInt8(new_vcb + vcbVN + i, BRIDGE_VOLUME_NAME[i]);
            WriteMacInt16(new_vcb + vcbFSID, BRIDGE_FSID);

            // Add VCB to queue
            memset(&r, 0, sizeof(r));
            r.d[0] = drive_number;
            r.a[0] = bfs_data + bfsReturn;
            r.a[1] = new_vcb;
            Execute68k(bfs_data + bfsAddNewVCB, &r);
            if (r.d[0] & 0xffff) return static_cast<int16_t>(r.d[0]);
            int16_t vRefNum = static_cast<int16_t>(ReadMacInt32(bfs_data + bfsReturn));

            // Post disk insert event
            memset(&r, 0, sizeof(r));
            r.d[0] = drive_number;
            r.a[0] = 7;  // diskEvent
            Execute68kTrap(0xa02f, &r);  // PostEvent()

            WriteMacInt16(paramBlock + ioVRefNum, vRefNum);
            fprintf(stderr, "[BridgeFS] Volume mounted: vRefNum=%d, vcb=0x%08X\n", vRefNum, new_vcb);
            return 0;
        }

        case 0x001B: { // kFSMGetVolParms
            uint32_t actual = ReadMacInt32(paramBlock + ioReqCount);
            if (actual > 20) actual = 20;  // SIZEOF_GetVolParmsInfoBuffer
            WriteMacInt32(paramBlock + ioActCount, actual);
            uint32_t buf = ReadMacInt32(paramBlock + ioBuffer);
            if (actual > 0) WriteMacInt16(buf + 0, 2);     // vMVersion = 2
            if (actual > 2) WriteMacInt32(buf + 2,
                kNoMiniFndr | kNoVNEdit | kNoLclSync | kTrshOffLine | kNoSwitchTo | kNoBootBlks | kNoSysDir | kHasExtFSVol);
            if (actual > 6) WriteMacInt32(buf + 6, 0);      // vMLocalHand
            if (actual > 10) WriteMacInt32(buf + 10, 0);     // vMServerAdr
            return 0;
        }

        case 0xA407: // kFSMHGetVInfo (hierarchical GetVInfo)
            return bridge_get_vol_info(paramBlock, vcb);

        case 0xA00E: // kFSMGetFCBInfo
            return -50;

        default:
            if (call_count <= 20)
                fprintf(stderr, "[BridgeFS] UNHANDLED sel=0x%04X\n", selectCode);
            return -50; // paramErr — unsupported operation
    }
}

// ============================================================================
// Installation — register with Mac File System Manager
// ============================================================================

static uint16_t make_bridge_emulop(uint16_t op) {
    return platform_make_emulop(op);
}

extern "C" void InstallBridgeFS(void) {
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled) return;

    // Initialize BridgeFS in-memory store
    if (!g_bridge_fs)
        g_bridge_fs = new BridgeFS();

    M68kRegisters r;

    fprintf(stderr, "[BridgeFS] Installing Bridge filesystem\n");

    // Set up volume name
    memset(BRIDGE_VOLUME_NAME, 0, 32);
    BRIDGE_VOLUME_NAME[0] = 6;  // Pascal string length
    memcpy(BRIDGE_VOLUME_NAME + 1, "Bridge", 6);

    memset(BRIDGE_FS_NAME, 0, 32);
    BRIDGE_FS_NAME[0] = 9;
    memcpy(BRIDGE_FS_NAME + 1, "Bridge FS", 9);

    // Check FSM availability
    memset(&r, 0, sizeof(r));
    r.d[0] = 0x66732020;  // gestaltFSAttr = 'fs  '
    Execute68kTrap(0xa1ad, &r);  // Gestalt()
    if ((r.d[0] & 0xffff) || !(r.a[0] & (1 << 3 /*gestaltHasFileSystemManager*/))) {
        fprintf(stderr, "[BridgeFS] No FSM, skipping\n");
        return;
    }

    // Allocate FS stack
    memset(&r, 0, sizeof(r));
    r.d[0] = STACK_SIZE;
    Execute68kTrap(0xa71e, &r);  // NewPtrSysClear
    if (r.a[0] == 0) { fprintf(stderr, "[BridgeFS] stack alloc failed\n"); return; }
    uint32_t fs_stack = r.a[0];

    // Allocate data block
    memset(&r, 0, sizeof(r));
    r.d[0] = SIZEOF_bfsdat;
    Execute68kTrap(0xa71e, &r);  // NewPtrSysClear
    if (r.a[0] == 0) { fprintf(stderr, "[BridgeFS] data alloc failed\n"); return; }
    bfs_data = r.a[0];

    // Write 68k stubs — same pattern as ExtFS
    int p = bfs_data + bfsCommProcStub;
    WriteMacInt16(p, make_bridge_emulop(M68K_EMUL_OP_BRIDGE_COMM)); p += 2;
    WriteMacInt16(p, 0x4e74); p += 2;
    WriteMacInt16(p, 10); p += 2;

    p = bfs_data + bfsHFSProcStub;
    WriteMacInt16(p, make_bridge_emulop(M68K_EMUL_OP_BRIDGE_HFS)); p += 2;
    WriteMacInt16(p, 0x4e74); p += 2;
    WriteMacInt16(p, 16);

    // Write 68k utility wrappers (identical to ExtFS)
    p = bfs_data + bfsAllocateVCB;
    WriteMacInt16(p, 0x4267); p+= 2;  // clr.w -(sp)
    WriteMacInt16(p, 0x2f08); p+= 2;  // move.l a0,-(sp)
    WriteMacInt16(p, 0x2f09); p+= 2;  // move.l a1,-(sp)
    WriteMacInt16(p, 0x4267); p+= 2;  // clr.w -(sp)
    WriteMacInt16(p, 0x7006); p+= 2;  // UTAllocateVCB
    WriteMacInt16(p, 0xa824); p+= 2;  // FSMgr
    WriteMacInt16(p, 0x301f); p+= 2;  // move.w (sp)+,d0
    WriteMacInt16(p, 0x4e75); p+= 2;

    p = bfs_data + bfsAddNewVCB;
    WriteMacInt16(p, 0x4267); p+= 2;
    WriteMacInt16(p, 0x3f00); p+= 2;
    WriteMacInt16(p, 0x2f08); p+= 2;
    WriteMacInt16(p, 0x2f09); p+= 2;
    WriteMacInt16(p, 0x7007); p+= 2;
    WriteMacInt16(p, 0xa824); p+= 2;
    WriteMacInt16(p, 0x301f); p+= 2;
    WriteMacInt16(p, 0x4e75); p+= 2;

    // UTAllocateFCB
    p = bfs_data + bfsAllocateFCB;
    WriteMacInt16(p, 0x4267); p+= 2;
    WriteMacInt16(p, 0x2f08); p+= 2;
    WriteMacInt16(p, 0x2f09); p+= 2;
    WriteMacInt16(p, 0x7000); p+= 2;  // UTAllocateFCB
    WriteMacInt16(p, 0xa824); p+= 2;
    WriteMacInt16(p, 0x301f); p+= 2;
    WriteMacInt16(p, 0x4e75); p+= 2;

    // UTReleaseFCB
    p = bfs_data + bfsReleaseFCB;
    WriteMacInt16(p, 0x4267); p+= 2;
    WriteMacInt16(p, 0x3f00); p+= 2;
    WriteMacInt16(p, 0x7001); p+= 2;  // UTReleaseFCB
    WriteMacInt16(p, 0xa824); p+= 2;
    WriteMacInt16(p, 0x301f); p+= 2;
    WriteMacInt16(p, 0x4e75); p+= 2;

    // UTResolveFCB
    p = bfs_data + bfsResolveFCB;
    WriteMacInt16(p, 0x4267); p+= 2;
    WriteMacInt16(p, 0x3f00); p+= 2;
    WriteMacInt16(p, 0x2f08); p+= 2;
    WriteMacInt16(p, 0x7005); p+= 2;  // UTResolveFCB
    WriteMacInt16(p, 0xa824); p+= 2;
    WriteMacInt16(p, 0x301f); p+= 2;
    WriteMacInt16(p, 0x4e75); p+= 2;

    // UTAdjustEOF
    p = bfs_data + bfsAdjustEOF;
    WriteMacInt16(p, 0x4267); p+= 2;
    WriteMacInt16(p, 0x3f00); p+= 2;
    WriteMacInt16(p, 0x7010); p+= 2;  // UTAdjustEOF
    WriteMacInt16(p, 0xa824); p+= 2;
    WriteMacInt16(p, 0x301f); p+= 2;
    WriteMacInt16(p, 0x4e75); p+= 2;

    // Set up drive status
    WriteMacInt8(bfs_data + bfsDrvStatus + dsDiskInPlace, 8);  // Fixed disk
    WriteMacInt8(bfs_data + bfsDrvStatus + dsInstalled, 1);
    WriteMacInt16(bfs_data + bfsDrvStatus + dsQType, hard20);
    WriteMacInt16(bfs_data + bfsDrvStatus + dsDriveSize, 0xFFFF);
    WriteMacInt16(bfs_data + bfsDrvStatus + dsDriveS1, 0);
    WriteMacInt16(bfs_data + bfsDrvStatus + dsQFSID, BRIDGE_FSID);

    // Add drive
    drive_number = FindFreeDriveNumber(1);
    memset(&r, 0, sizeof(r));
    r.d[0] = (drive_number << 16) | (DiskRefNum & 0xffff);
    r.a[0] = bfs_data + bfsDrvStatus + dsQLink;
    Execute68kTrap(0xa04e, &r);  // AddDrive()

    // Init FSDRec
    WriteMacInt16(bfs_data + bfsFSD + fsdLength, SIZEOF_FSDRec);
    WriteMacInt16(bfs_data + bfsFSD + fsdVersion, 1);
    WriteMacInt16(bfs_data + bfsFSD + fileSystemFSID, BRIDGE_FSID);
    Host2Mac_memcpy(bfs_data + bfsFSD + fileSystemName, BRIDGE_FS_NAME, 32);
    WriteMacInt32(bfs_data + bfsFSD + fileSystemCommProc, bfs_data + bfsCommProcStub);
    WriteMacInt32(bfs_data + bfsFSD + fsdHFSCI + compInterfProc, bfs_data + bfsHFSProcStub);
    WriteMacInt32(bfs_data + bfsFSD + fsdHFSCI + stackTop, fs_stack + STACK_SIZE);
    WriteMacInt32(bfs_data + bfsFSD + fsdHFSCI + stackSize, STACK_SIZE);
    WriteMacInt32(bfs_data + bfsFSD + fsdHFSCI + idSector, (uint32_t)-1);

    // InstallFS
    memset(&r, 0, sizeof(r));
    r.a[0] = bfs_data + bfsFSD;
    r.d[0] = 0;  // InstallFS
    Execute68kTrap(0xa0ac, &r);  // FSMDispatch()
    fprintf(stderr, "[BridgeFS] InstallFS() returned %d\n", (int16_t)(r.d[0] & 0xffff));

    // Enable HFS component
    uint32_t mask = ReadMacInt32(bfs_data + bfsFSD + fsdHFSCI + compInterfMask);
    WriteMacInt32(bfs_data + bfsFSD + fsdHFSCI + compInterfMask,
                  mask | fsmComponentEnableMask | hfsCIResourceLoadedMask | hfsCIDoesHFSMask);
    memset(&r, 0, sizeof(r));
    r.a[0] = bfs_data + bfsFSD;
    r.d[3] = SIZEOF_FSDRec;
    r.d[4] = BRIDGE_FSID;
    r.d[0] = 5;  // SetFSInfo
    Execute68kTrap(0xa0ac, &r);
    fprintf(stderr, "[BridgeFS] SetFSInfo() returned %d\n", (int16_t)(r.d[0] & 0xffff));

    // Mount volume directly — allocate VCB and add to queue
    // (PBVolumeMount doesn't call our HFS dispatch for some reason,
    //  so we do the mount ourselves, same as ExtFS's fs_volume_mount)
    {
        // Allocate VCB
        memset(&r, 0, sizeof(r));
        r.a[0] = bfs_data + bfsReturn;
        r.a[1] = bfs_data + bfsReturn + 2;
        Execute68k(bfs_data + bfsAllocateVCB, &r);
        if (r.d[0] & 0xffff) {
            fprintf(stderr, "[BridgeFS] UTAllocateVCB failed: %d\n", (int16_t)(r.d[0] & 0xffff));
            return;
        }
        uint32_t vcb = ReadMacInt32(bfs_data + bfsReturn + 2);

        // Init VCB
        WriteMacInt16(vcb + vcbSigWord, 0x4244);
        WriteMacInt16(vcb + vcbNmFls, 0);
        WriteMacInt16(vcb + vcbNmRtDirs, 0);
        WriteMacInt16(vcb + vcbNmAlBlks, 0xFFFF);
        WriteMacInt32(vcb + vcbAlBlkSiz, 0x4000);
        WriteMacInt32(vcb + vcbClpSiz, 0x4000);
        WriteMacInt16(vcb + vcbFreeBks, 0xFFFF);
        for (int i = 0; i < 28; i++)
            WriteMacInt8(vcb + vcbVN + i, BRIDGE_VOLUME_NAME[i]);
        WriteMacInt16(vcb + vcbFSID, BRIDGE_FSID);

        // Add VCB to queue
        memset(&r, 0, sizeof(r));
        r.d[0] = drive_number;
        r.a[0] = bfs_data + bfsReturn;
        r.a[1] = vcb;
        Execute68k(bfs_data + bfsAddNewVCB, &r);
        if (r.d[0] & 0xffff) {
            fprintf(stderr, "[BridgeFS] UTAddNewVCB failed: %d\n", (int16_t)(r.d[0] & 0xffff));
            return;
        }
        int16_t vRefNum = static_cast<int16_t>(ReadMacInt32(bfs_data + bfsReturn));

        // Post disk insert event
        memset(&r, 0, sizeof(r));
        r.d[0] = drive_number;
        r.a[0] = 7;  // diskEvent
        Execute68kTrap(0xa02f, &r);  // PostEvent()

        fprintf(stderr, "[BridgeFS] Volume mounted: vRefNum=%d, vcb=0x%08X\n", vRefNum, vcb);
    }

    fprintf(stderr, "[BridgeFS] Installed: drive=%d, fsid=0x%04X\n", drive_number, BRIDGE_FSID);
}

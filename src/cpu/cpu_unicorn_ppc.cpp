// Unicorn PPC backend — full integration with the Mac-phoenix PPC runtime.
//
// Alternative PPC backend using Unicorn Engine's TCG-based PPC target instead
// of KPX's SheepShaver-derived interpreter. Coexists with KPX and is selected
// via `--backend unicorn --arch ppc`.
//
// Structurally this file mirrors src/cpu/kpx/cpu_ppc_kpx.cpp. The key mapping
// is KPX's powerpc_cpu member functions -> Unicorn uc_emu_start / uc_reg_*:
//
//   KPX                         Unicorn PPC
//   ----                        -----------
//   ppc_cpu->execute(entry)     uc_reg_write(PC,entry); uc_emu_start(uc,…)
//   ppc_cpu->gpr(n)             uc_reg_read(uc, UC_PPC_REG_0+n, …)
//   ppc_cpu->spcflags().set(…)  pending_* flags, uc_emu_stop
//   sheepshaver_cpu::interrupt  uppc_interrupt
//   sheepshaver_cpu::execute_68k uppc_execute_68k
//   sheepshaver_cpu::execute_ppc uppc_execute_ppc
//   execute_sheep (major-op-6)  uppc_mac_emulop_cb (via translate.c patch)
//
// See docs/ppc/UnicornPpcPlan.md for the design and milestone plan.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>
#include <unordered_map>

#include <sys/mman.h>
#include <unistd.h>

#include "platform.h"
#include "m68k_registers.h"
#include "unicorn/unicorn.h"

// ----- Extern callbacks/hooks into shared code ------------------------------

// Installs a host callback invoked from helper_mac_emulop when the guest
// executes a major-opcode-6 instruction (mac-phoenix addition in uc.c).
extern "C" void uc_ppc_set_mac_emulop_cb(
    uc_engine *uc,
    void (*cb)(struct uc_struct *uc, uint32_t pc, uint32_t opcode));

// KPX-shared PPC globals/setup (defined in src/core/cpu_context.cpp and
// src/cpu/kpx/cpu_ppc_kpx.cpp). We reuse them to avoid duplicating ROM
// loading and the REAL_ADDRESSING mmap dance.
namespace ppc {
    extern uint32_t RAMBase, RAMSize, ROMBase, KernelDataAddr;
    extern uint8_t *RAMBaseHost, *ROMBaseHost;
}

// Shared tick-inhibit flag — cleared by OP_RESET when nanokernel is ready.
extern bool tick_inhibit;

// SignalStackBase(): upper bound of the interrupt fake stack (set during PPC
// init in cpu_ppc_kpx.cpp via kpx_set_signal_stack). Global C++ linkage.
extern uintptr_t SignalStackBase(void);

// Timer helper from src/core/timer_unix.cpp, shared with KPX tick thread.
extern uint64_t GetTicks_usec(void);

// Interrupt-flag plumbing (uae_wrapper.cpp). INTFLAG_VIA is the 60Hz tick
// signal the 68k emulator and nanokernel both watch.
extern "C" void SetInterruptFlag(uint32_t flag);
extern "C" void ClearInterruptFlag(uint32_t flag);
#ifndef INTFLAG_VIA
#define INTFLAG_VIA 0x01   // matches src/common/include/main.h
#endif
#ifndef INTFLAG_60HZ
#define INTFLAG_60HZ 0x04
#endif

// ROMType — set by PatchROM_PPC in rom_patches_ppc.cpp (namespace ppc).
namespace ppc { extern int ROMType; }
#define ROMTYPE_NEWWORLD 6

// Native helpers invoked by EXEC_NATIVE. DoPatchNameRegistry is in the global
// namespace; VideoInstallAccel/VideoVBL live in `namespace ppc`.
extern void DoPatchNameRegistry(void);
struct M68kRegisters;
namespace ppc {
    extern void VideoInstallAccel(void);
    extern void VideoVBL(void);
    extern void EmulOp(M68kRegisters *r, uint32_t pc, int selector);
}

// ----- Local constants (mirror src/cpu/kpx/compat/*) ------------------------

// POWERPC_EMUL_OP sentinel — major opcode 6 (reserved in base PPC ISA).
// Kernel code inserts `0x18000000 | (selector & 0x3FFFFFF)` instructions; the
// translate.c patch routes them to helper_mac_emulop, which calls our
// uppc_mac_emulop_cb below. Selectors match SheepShaver's NATIVE_* /
// EMUL_OP_* / EXEC_RETURN layout (see docs/ppc/UnicornPpcPlan.md §5).
#define POWERPC_EMUL_OP    0x18000000u
#define POWERPC_EXEC_RETURN (POWERPC_EMUL_OP | 1u)
#define EMUL_OP_SEL_MASK   0x0000003Fu  // low 6 bits of the opcode

// XLM fields from kpx/compat/xlowmem.h. These live in the 0x2800..0x28ff low-
// memory page (REAL_ADDRESSING, Mac addr = host addr). A previous version of
// this file had them as 0x68ffec00..0x68ffec1c which was wrong on both ends —
// KPX init_ppc.cpp writes M68K_EMUL_RETURN to 0x284c, so pushing any other
// address as the Execute68k return trampoline lands the 68k RTS on arbitrary
// bytes and drives r1 to zero a few iterations later.
#define XLM_RUN_MODE          0x2810u
#define XLM_68K_R25           0x2814u
#define XLM_IRQ_NEST          0x2818u
#define XLM_EXEC_RETURN_OPCODE 0x284cu

#define MODE_68K      0
#define MODE_NATIVE   1
#define MODE_EMUL_OP  2

#define KERNEL_DATA_BASE      0x68ffe000u

// DR probe regions — munmap'd during init so nanokernel faults, then remapped
// as RW after first IRQ (see §3 of plan).
#define DR_EMUL_BASE   0x68070000u
#define DR_EMUL_SIZE   0x00010000u
#define DR_CACHE_BASE  0x69000000u
#define DR_CACHE_SIZE  0x00080000u

// NativeOp selector table — mirrors enum in kpx/compat/thunks.h. Used for
// readable EXEC_NATIVE dispatch logs. Only referenced by selector number.
// The actual handler functions live in thunks_ppc.cpp / video_ppc.cpp / etc.
enum {
    NATIVE_PATCH_NAME_REGISTRY = 0,
    NATIVE_VIDEO_INSTALL_ACCEL,
    NATIVE_VIDEO_VBL,
    NATIVE_VIDEO_DO_DRIVER_IO,
    NATIVE_ETHER_AO_GET_HWADDR,
    NATIVE_ETHER_AO_ADD_MULTI,
    NATIVE_ETHER_AO_DEL_MULTI,
    NATIVE_ETHER_AO_SEND_PACKET,
    NATIVE_ETHER_IRQ,
    NATIVE_ETHER_INIT,
    NATIVE_ETHER_TERM,
    NATIVE_ETHER_OPEN,
    NATIVE_ETHER_CLOSE,
    NATIVE_ETHER_WPUT,
    NATIVE_ETHER_RSRV,
    NATIVE_SERIAL_NOTHING,
    NATIVE_SERIAL_OPEN,
    NATIVE_SERIAL_PRIME_IN,
    NATIVE_SERIAL_PRIME_OUT,
    NATIVE_SERIAL_CONTROL,
    NATIVE_SERIAL_STATUS,
    NATIVE_SERIAL_CLOSE,
    NATIVE_GET_RESOURCE,
    NATIVE_GET_1_RESOURCE,
    NATIVE_GET_IND_RESOURCE,
    NATIVE_GET_1_IND_RESOURCE,
    NATIVE_R_GET_RESOURCE,
    NATIVE_MAKE_EXECUTABLE,
    NATIVE_CHECK_LOAD_INVOC,
    NATIVE_NQD_SYNC_HOOK,
    NATIVE_NQD_BITBLT_HOOK,
    NATIVE_NQD_FILLRECT_HOOK,
    NATIVE_NQD_UNKNOWN_HOOK,
    NATIVE_NQD_BITBLT,
    NATIVE_NQD_INVRECT,
    NATIVE_NQD_FILLRECT,
    NATIVE_NAMED_CHECK_LOAD_INVOC,
    NATIVE_GET_NAMED_RESOURCE,
    NATIVE_GET_1_NAMED_RESOURCE,
    NATIVE_OP_MAX
};

extern "C" {

// ----- REAL_ADDRESSING memory accessors -------------------------------------
// Mac addr == host addr; big-endian guest on little-endian host.

static uint8_t  uppc_mem_read_byte(uint32_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}
static uint16_t uppc_mem_read_word(uint32_t addr)
{
    uint16_t v = *(volatile uint16_t *)(uintptr_t)addr;
    return __builtin_bswap16(v);
}
static uint32_t uppc_mem_read_long(uint32_t addr)
{
    uint32_t v = *(volatile uint32_t *)(uintptr_t)addr;
    return __builtin_bswap32(v);
}
static void uppc_mem_write_byte(uint32_t addr, uint8_t val)
{
    *(volatile uint8_t *)(uintptr_t)addr = val;
}
static void uppc_mem_write_word(uint32_t addr, uint16_t val)
{
    *(volatile uint16_t *)(uintptr_t)addr = __builtin_bswap16(val);
}
static void uppc_mem_write_long(uint32_t addr, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)addr = __builtin_bswap32(val);
}
static uint8_t *uppc_mem_mac_to_host(uint32_t addr) { return (uint8_t *)(uintptr_t)addr; }
static uint32_t uppc_mem_host_to_mac(uint8_t *ptr)  { return (uint32_t)(uintptr_t)ptr; }

// Shorthand mirrors of ReadMacInt32 / WriteMacInt32 used inside this file.
static inline uint32_t ReadMac32(uint32_t addr)  { return uppc_mem_read_long(addr); }
static inline void     WriteMac32(uint32_t addr, uint32_t v) { uppc_mem_write_long(addr, v); }
static inline uint16_t ReadMac16(uint32_t addr)  { return uppc_mem_read_word(addr); }
static inline void     WriteMac16(uint32_t addr, uint16_t v) { uppc_mem_write_word(addr, v); }

// ----- Unicorn engine state -------------------------------------------------

static uc_engine   *g_uc = nullptr;
static volatile bool g_stop_requested = false;  // parent asked us to stop
static std::atomic<bool> g_pending_irq{false};  // tick thread set; main loop handles
static std::atomic<bool> g_tick_thread_running{false};
static bool g_dr_probes_remapped = false;
static uint64_t g_emulop_count = 0;

// ----- Register helpers (uc_reg_read/write wrappers) -----------------------

static inline uint32_t rd_reg(int r)
{
    uint32_t v = 0;
    if (g_uc) uc_reg_read(g_uc, r, &v);
    return v;
}
static inline void wr_reg(int r, uint32_t v)
{
    if (g_uc) uc_reg_write(g_uc, r, &v);
}
static inline uint32_t rd_gpr(int n) { return rd_reg(UC_PPC_REG_0 + n); }
static inline void     wr_gpr(int n, uint32_t v) { wr_reg(UC_PPC_REG_0 + n, v); }
static inline uint32_t rd_pc(void)  { return rd_reg(UC_PPC_REG_PC); }
static inline void     wr_pc(uint32_t v) { wr_reg(UC_PPC_REG_PC, v); }
static inline uint32_t rd_lr(void)  { return rd_reg(UC_PPC_REG_LR); }
static inline void     wr_lr(uint32_t v) { wr_reg(UC_PPC_REG_LR, v); }
static inline uint32_t rd_ctr(void) { return rd_reg(UC_PPC_REG_CTR); }
static inline void     wr_ctr(uint32_t v) { wr_reg(UC_PPC_REG_CTR, v); }
static inline uint32_t rd_cr(void)  { return rd_reg(UC_PPC_REG_CR); }
static inline void     wr_cr(uint32_t v) { wr_reg(UC_PPC_REG_CR, v); }
static inline uint32_t rd_xer(void) { return rd_reg(UC_PPC_REG_XER); }
static inline void     wr_xer(uint32_t v) { wr_reg(UC_PPC_REG_XER, v); }

// Decode a PPC load/store at the given PC and "skip" it in the KPX-SIGSEGV
// shape: for loads, zero the destination GPR; for update-mode, set ra to the
// effective address; for stores, do nothing; then advance PC by 4. Mirrors
// src/common/sigsegv.cpp:powerpc_skip_instruction / powerpc_decode_instruction.
//
// Returns true if the instruction was a recognized memory op and PC was
// advanced, false if we don't know how to skip it (caller should bail).
static bool uppc_skip_memop_at(uint32_t pc)
{
    uint32_t opcode = 0;
    if (uc_mem_read(g_uc, pc, &opcode, 4) != UC_ERR_OK) return false;
    opcode = __builtin_bswap32(opcode);

    const uint32_t primop = opcode >> 26;
    const uint32_t rd     = (opcode >> 21) & 0x1f;
    const uint32_t ra     = (opcode >> 16) & 0x1f;
    const uint32_t rb     = (opcode >> 11) & 0x1f;
    const uint32_t exop   = (opcode >> 1) & 0x3ff;
    const int32_t  imm    = (int16_t)(opcode & 0xffff);

    enum { TR_NONE = 0, TR_LOAD, TR_STORE };
    enum { MD_NONE = 0, MD_NORM, MD_U, MD_X, MD_UX };

    int transfer = TR_NONE;
    int mode     = MD_NONE;

    switch (primop) {
    case 31: // X-form: lXzx / lXzux / stXx / stXux
        switch (exop) {
        case 23: case 87: case 279: case 343:
            transfer = TR_LOAD;  mode = MD_X;  break;   // lwzx/lbzx/lhzx/lhax
        case 55: case 119: case 311: case 375:
            transfer = TR_LOAD;  mode = MD_UX; break;   // lwzux/lbzux/lhzux/lhaux
        case 151: case 215: case 407:
            transfer = TR_STORE; mode = MD_X;  break;   // stwx/stbx/sthx
        case 183: case 247: case 439:
            transfer = TR_STORE; mode = MD_UX; break;   // stwux/stbux/sthux
        }
        break;
    case 32: case 34: case 40: case 42:                 // lwz/lbz/lhz/lha
        transfer = TR_LOAD;  mode = MD_NORM; break;
    case 33: case 35: case 41: case 43:                 // lwzu/lbzu/lhzu/lhau
        transfer = TR_LOAD;  mode = MD_U;    break;
    case 36: case 38: case 44:                          // stw/stb/sth
        transfer = TR_STORE; mode = MD_NORM; break;
    case 37: case 39: case 45:                          // stwu/stbu/sthu
        transfer = TR_STORE; mode = MD_U;    break;
    default: break;
    }

    if (transfer == TR_NONE) return false;

    uint32_t addr = 0;
    switch (mode) {
    case MD_X: case MD_UX:
        addr = (ra == 0 ? 0 : rd_gpr(ra)) + rd_gpr(rb);
        break;
    case MD_NORM: case MD_U:
        addr = (ra == 0 ? 0 : rd_gpr(ra)) + (uint32_t)imm;
        break;
    }

    if (mode == MD_U || mode == MD_UX) wr_gpr(ra, addr);
    if (transfer == TR_LOAD) wr_gpr(rd, 0);

    wr_pc(pc + 4);
    return true;
}

// ----- Memory mapping -------------------------------------------------------

static bool map_region(uc_engine *uc, uint64_t addr, size_t size,
                       uint32_t perms, void *host_ptr, const char *label)
{
    uc_err err = uc_mem_map_ptr(uc, addr, size, perms, host_ptr);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[Unicorn-PPC] uc_mem_map_ptr(%s @0x%08lx size=0x%zx) failed: %s\n",
                label, (unsigned long)addr, size, uc_strerror(err));
        return false;
    }
    fprintf(stderr, "[Unicorn-PPC] mapped %-12s mac=0x%08lx size=0x%08zx host=%p perms=0x%x\n",
            label, (unsigned long)addr, size, host_ptr, perms);
    return true;
}

// Called from the first-IRQ boundary to reinstate DR probe regions as normal
// RW memory. KPX relies on a SIGSEGV handler that skips the probe instruction
// then remaps on first HandleInterrupt (cpu_ppc_kpx.cpp:1202). Under Unicorn
// we map them RW up front (the nanokernel sees zeros, matching KPX's final
// state), so this is currently a logging no-op — kept for symmetry and for
// when we move to uc_mmio_map-based probe detection.
static void uppc_remap_dr_probes_once(void)
{
    if (g_dr_probes_remapped) return;
    g_dr_probes_remapped = true;
    fprintf(stderr, "[Unicorn-PPC] DR probe remap (first IRQ) — already RW, nothing to do\n");
}

// ----- EmulOp dispatch (called from helper_mac_emulop) ---------------------

// Marshal PPC GPRs into M68kRegisters before invoking ppc_emulop_handler,
// mirroring sheepshaver_cpu::execute_emul_op exactly.
static void uppc_dispatch_emul_op(uint32_t pc, uint32_t opcode)
{
    extern Platform g_platform;
    if (!g_platform.ppc_emulop_handler) {
        fprintf(stderr, "[Unicorn-PPC] FATAL: ppc_emulop_handler not set\n");
        abort();
    }

    // Selector: low 6 bits minus 3 (cases 0..2 are QUIT/EXEC_RET/EXEC_NAT).
    uint32_t emul_op = (opcode & EMUL_OP_SEL_MASK) - 3;

    struct M68kRegisters r68;
    std::memset(&r68, 0, sizeof(r68));

    WriteMac32(XLM_68K_R25, rd_gpr(25));
    WriteMac32(XLM_RUN_MODE, MODE_EMUL_OP);

    for (int i = 0; i < 8; i++)
        r68.d[i] = rd_gpr(8 + i);
    for (int i = 0; i < 7; i++)
        r68.a[i] = rd_gpr(16 + i);
    r68.a[7] = rd_gpr(1);

    uint32_t saved_cr  = rd_cr() & 0xff9fffffu;
    uint32_t saved_xer = rd_xer();

    g_platform.ppc_emulop_handler(&r68, rd_gpr(24), emul_op);

    wr_cr(saved_cr);
    wr_xer(saved_xer);

    for (int i = 0; i < 8; i++)
        wr_gpr(8 + i, r68.d[i]);
    for (int i = 0; i < 7; i++)
        wr_gpr(16 + i, r68.a[i]);
    wr_gpr(1, r68.a[7]);

    WriteMac32(XLM_RUN_MODE, MODE_68K);
}

// EXEC_NATIVE — dispatch one native-op selector. The individual routines are
// shared between backends (defined in thunks_ppc.cpp / video_ppc.cpp /
// ether_ppc.cpp). We mirror the KPX switch here; selectors we don't wire yet
// are logged as stubs to be triaged during boot debugging.
//
// Many of these take GPR inputs / return via GPR3 — matches KPX semantics.
static void uppc_dispatch_native_op(uint32_t pc, uint32_t opcode)
{
    uint32_t selector = (opcode >> 6) & 0x3F;
    bool return_via_lr = (opcode >> 12) & 1;

    // These handlers all live outside this file. We forward-declare only the
    // ones we can call without dragging in KPX headers; the rest stay logged
    // until the debug pass wires them in.
    //
    // NOTE: many native ops rely on global state set up by KPX init; they are
    // expected to be available at this point because cpu_context.cpp runs
    // InitAll_PPC (which calls PatchROM_PPC) before invoking cpu_init on any
    // backend.
    switch (selector) {
    case NATIVE_PATCH_NAME_REGISTRY:
        ::DoPatchNameRegistry();
        break;
    case NATIVE_VIDEO_INSTALL_ACCEL:
        ppc::VideoInstallAccel();
        break;
    case NATIVE_VIDEO_VBL:
        ppc::VideoVBL();
        break;
    default:
        // Remaining selectors (NATIVE_VIDEO_DO_DRIVER_IO, NATIVE_ETHER_*,
        // NATIVE_SERIAL_*, NATIVE_GET_RESOURCE family, NATIVE_NQD_*) each need
        // their own argument marshalling. Deferred to the debug pass — log so
        // the first divergence is visible.
        fprintf(stderr, "[Unicorn-PPC] EXEC_NATIVE selector=%u (stub) @pc=0x%08x\n",
                selector, pc);
        break;
    }

    // Advance PC per bit 19 of the opcode (FN_field in SheepShaver terms).
    if (return_via_lr)
        wr_pc(rd_lr());
    // else: helper already advanced env->nip to pc+4.
    (void)return_via_lr;
}

static void uppc_mac_emulop_cb(struct uc_struct *uc, uint32_t pc, uint32_t opcode)
{
    g_emulop_count++;
    uint32_t sel = opcode & EMUL_OP_SEL_MASK;
    switch (sel) {
    case 0: // EMUL_RETURN — QuitEmulator
        fprintf(stderr, "[Unicorn-PPC] EmulOp QUIT @pc=0x%08x\n", pc);
        g_stop_requested = true;
        uc_emu_stop(uc);
        return;

    case 1: // EXEC_RETURN — sentinel set by interrupt()/execute_ppc() in LR
        uc_emu_stop(uc);
        return;

    case 2: // EXEC_NATIVE
        uppc_dispatch_native_op(pc, opcode);
        return;

    default: // EMUL_OP (selector = (opcode & 0x3f) - 3)
        uppc_dispatch_emul_op(pc, opcode);
        return;
    }
}

// ----- Platform API: lifecycle ---------------------------------------------

static bool uppc_cpu_init(void)
{
    using namespace ppc;

    fprintf(stderr, "[Unicorn-PPC] cpu_init: opening engine...\n");

    uc_err err = uc_open(UC_ARCH_PPC,
                         (uc_mode)(UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN), &g_uc);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[Unicorn-PPC] uc_open failed: %s\n", uc_strerror(err));
        return false;
    }

    // Install EmulOp dispatch callback. Major-opcode-6 instructions routed
    // through helper_mac_emulop call this (see mac_emulop_helper.c).
    uc_ppc_set_mac_emulop_cb(g_uc, uppc_mac_emulop_cb);

    // Debug: trace instructions executed until we hit the divergence at
    // 0x503141cc. Controlled by MP_UPPC_TRACE env var (prints count, stops
    // after N insns). Guarded so production paths are free.
    // Narrow code hooks: fire only when PC is within a specific range. Much
    // cheaper than a wildcard hook (which adds overhead per-insn). Uses one
    // hook per range of interest.
    {
        // Dump r0..r31 + LR + CTR when entering the jump-to-emul patch and
        // again at the 68k emulator entry. Lets us see which register holds
        // the 68k PC going into the DR emulator.
        static auto trc_patch_cb = [](uc_engine *uc, uint64_t addr, uint32_t, void *) {
            uint32_t r[32] = {0}, lr = 0, ctr = 0;
            for (int i = 0; i < 32; i++) uc_reg_read(uc, UC_PPC_REG_0 + i, &r[i]);
            uc_reg_read(uc, UC_PPC_REG_LR, &lr);
            uc_reg_read(uc, UC_PPC_REG_CTR, &ctr);
            fprintf(stderr, "[TRC @0x%08llx] lr=%08x ctr=%08x\n",
                    (unsigned long long)addr, lr, ctr);
            for (int i = 0; i < 32; i += 8) {
                fprintf(stderr, "  r%02d..r%02d: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                        i, i+7, r[i], r[i+1], r[i+2], r[i+3],
                        r[i+4], r[i+5], r[i+6], r[i+7]);
            }
        };
        uc_hook hh1 = 0, hh2 = 0, hh3 = 0;
        // Just before the bctr to 68k emulator.
        uc_hook_add(g_uc, &hh1, UC_HOOK_CODE,
                    (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))trc_patch_cb,
                    nullptr, 0x50314230, 0x50314230);
        // At the 68k emulator entry.
        uc_hook_add(g_uc, &hh2, UC_HOOK_CODE,
                    (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))trc_patch_cb,
                    nullptr, 0x5046e6f4, 0x5046e6f4);
        // At the 68k emulator's dispatch point where it reads gpr(24).
        uc_hook_add(g_uc, &hh3, UC_HOOK_CODE,
                    (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))trc_patch_cb,
                    nullptr, 0x5046e854, 0x5046e854);

        // Track the 68k PC (gpr 24) across the whole DR-emulator code region.
        // Log whenever r24 moves to a value that's not a small step from the
        // previous value (indicating a branch), or when it leaves the known
        // valid address ranges (RAM / ROM). Hook covers the whole emulator
        // span (0x50460000..0x504ff000 post-memcpy).
        static auto trc_dispatch_cb = [](uc_engine *uc, uint64_t addr, uint32_t, void *) {
            static uint64_t n = 0;
            static uint32_t prev_r24 = 0;
            static bool first = true;
            uint32_t r24 = 0;
            uc_reg_read(uc, UC_PPC_REG_24, &r24);
            bool bogus = !((r24 < 0x08800000) ||
                           (r24 >= 0x50000000 && r24 < 0x50500000));
            int32_t delta = (int32_t)(r24 - prev_r24);
            bool branch = !first && (delta < -16 || delta > 16);
            if (first || branch || bogus) {
                uint32_t r27 = 0, r29 = 0, lr = 0;
                uc_reg_read(uc, UC_PPC_REG_27, &r27);
                uc_reg_read(uc, UC_PPC_REG_29, &r29);
                uc_reg_read(uc, UC_PPC_REG_LR, &lr);
                // Keep a ring buffer of the last 32 iterations. When we hit
                // a BOGUS r24, dump the ring so we can see what sequence of
                // 68k instructions led to the divergence. Also peek at the
                // memory at r24 to capture the actual 68k opcode about to be
                // dispatched (the lha has not yet fired when the hook runs).
                uint32_t opcode_be = 0;
                if (r24 < 0x08800000 || (r24 >= 0x50000000 && r24 < 0x50500000)) {
                    uc_mem_read(uc, r24, &opcode_be, 4);
                }
                struct Entry { uint64_t n; uint32_t addr, r24, lr, prev, op; };
                static Entry ring[32]; static int rp = 0;
                ring[rp] = { n, (uint32_t)addr, r24, lr, prev_r24,
                             __builtin_bswap32(opcode_be) };
                rp = (rp + 1) & 31;
                if (bogus) {
                    fprintf(stderr, "[TRC-DISP] --- last 32 before bogus ---\n");
                    for (int i = 0; i < 32; i++) {
                        int idx = (rp + i) & 31;
                        fprintf(stderr, "[TRC-DISP %4llu] pc=0x%08x r24=%08x (prev=%08x) op@r24=%08x lr=%08x\n",
                                (unsigned long long)ring[idx].n, ring[idx].addr,
                                ring[idx].r24, ring[idx].prev, ring[idx].op, ring[idx].lr);
                    }
                }
                if (n < 30) {
                    fprintf(stderr, "[TRC-DISP %4llu] pc=0x%08llx r24=%08x (prev=%08x) r27=%08x lr=%08x%s\n",
                            (unsigned long long)n, (unsigned long long)addr,
                            r24, prev_r24, r27, lr,
                            bogus ? " <BOGUS>" : (branch ? " <BRANCH>" : ""));
                }
            }
            prev_r24 = r24;
            first = false;
            if (bogus) {
                static uint64_t bogus_n = 0;
                if (++bogus_n > 4) {
                    g_stop_requested = true;
                    uc_emu_stop(uc);
                }
            }
            n++;
        };
        // Install the dispatch-trace hook at every `lha r27, 0(r24)` site in
        // the DR emulator (found via static scan of the ROM). These are the
        // opcode-fetch points for the 68k emulation loop.
        static const uint32_t disp_sites[] = {
            0x50466e30, 0x50467ce4, 0x50467da0, 0x504688c4,
            0x50469604, 0x50469684, 0x504696e0, 0x50469740,
            0x5046979c, 0x5046987c, 0x5046d740, 0x5046ddcc,
            0x5046de30, 0x5046e0e4, 0x5046e854, 0x5047783c,
        };
        for (uint32_t site : disp_sites) {
            uc_hook hh = 0;
            uc_hook_add(g_uc, &hh, UC_HOOK_CODE,
                        (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))trc_dispatch_cb,
                        nullptr, site, site);
        }
    }

    // Unmapped-memory hook. KPX boots with a SIGSEGV handler that skips the
    // faulting instruction without touching the destination register or memory
    // — this is how the nanokernel's RAM-sizing probe loops at 0x50460c00,
    // 0x50313fc8, etc. terminate (the "did the write stick?" check fails when
    // the read is also skipped). We replicate that by returning false from the
    // hook so Unicorn halts with UC_ERR_{READ,WRITE}_UNMAPPED, then execute_fast
    // advances PC past the dead instruction and resumes. FETCH faults stay
    // fatal — jumping into unmapped territory is a real bug, not a probe.
    //
    // Accounting in g_unmapped_read_pc lets execute_fast know to skip and
    // avoids the probe-loop escaping as a "stuck PC" bailout.
    {
        static auto mem_unmapped_cb = [](uc_engine *uc, uc_mem_type type,
                                         uint64_t addr, int size,
                                         int64_t value, void *) -> bool {
            (void)value;
            if (type == UC_MEM_FETCH_UNMAPPED) {
                uint32_t pc = 0;
                uc_reg_read(uc, UC_PPC_REG_PC, &pc);
                fprintf(stderr, "[Unicorn-PPC] FETCH UNMAPPED @pc=0x%08x "
                                "addr=0x%010llx (fatal)\n",
                        pc, (unsigned long long)addr);
                g_stop_requested = true;
                uc_emu_stop(uc);
                return false;
            }
            {
                static uint64_t n = 0;
                if (n < 16) {
                    uint32_t pc = 0;
                    uc_reg_read(uc, UC_PPC_REG_PC, &pc);
                    fprintf(stderr, "[Unicorn-PPC] UNMAPPED %s pc=0x%08x target=0x%010llx size=%d\n",
                            (type == UC_MEM_READ_UNMAPPED) ? "READ" : "WRITE",
                            pc, (unsigned long long)addr, size);
                    // Dump all 32 GPRs + LR/CTR + the instruction word so we can
                    // decode which GPR holds the bogus effective address.
                    if (n < 2) {
                        uint32_t r[32] = {0};
                        for (int i = 0; i < 32; i++)
                            uc_reg_read(uc, UC_PPC_REG_0 + i, &r[i]);
                        uint32_t lr = 0, ctr = 0, insn_be = 0;
                        uc_reg_read(uc, UC_PPC_REG_LR, &lr);
                        uc_reg_read(uc, UC_PPC_REG_CTR, &ctr);
                        uc_mem_read(uc, pc, &insn_be, 4);
                        uint32_t insn = __builtin_bswap32(insn_be);
                        fprintf(stderr, "[Unicorn-PPC]   insn=0x%08x lr=0x%08x ctr=0x%08x\n",
                                insn, lr, ctr);
                        for (int i = 0; i < 32; i += 8) {
                            fprintf(stderr, "[Unicorn-PPC]   r%02d..r%02d: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                                    i, i+7, r[i], r[i+1], r[i+2], r[i+3],
                                    r[i+4], r[i+5], r[i+6], r[i+7]);
                        }
                    }
                }
                n++;
            }
            // Read/write to unmapped: let Unicorn raise UC_ERR_{READ,WRITE}_UNMAPPED.
            // execute_fast will bump PC by 4 and resume (KPX skip-on-SIGSEGV shape).
            return false;
        };
        uc_hook hook_unmapped = 0;
        uc_hook_add(g_uc, &hook_unmapped,
                    UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                    UC_HOOK_MEM_FETCH_UNMAPPED,
                    (void *)(bool (*)(uc_engine *, uc_mem_type, uint64_t,
                                      int, int64_t, void *))mem_unmapped_cb,
                    nullptr, 1, 0);

        // Watchpoint on ROM zero-padding region (0x50400000..0x50500000). We
        // want to see if the nanekernel populates code at 0x5046e6f4 etc.
        static auto wp_cb = [](uc_engine *uc, uc_mem_type type, uint64_t addr,
                               int size, int64_t value, void *) -> void {
            (void)type;
            static uint64_t n = 0;
            if (n < 32) {
                uint32_t pc = 0, lr = 0;
                uc_reg_read(uc, UC_PPC_REG_PC, &pc);
                uc_reg_read(uc, UC_PPC_REG_LR, &lr);
                fprintf(stderr, "[Unicorn-PPC] ROMZ-WRITE @0x%08llx val=0x%08llx size=%d pc=0x%08x lr=0x%08x\n",
                        (unsigned long long)addr, (unsigned long long)value, size, pc, lr);
            }
            n++;
        };
        uc_hook hook_wp = 0;
        uc_hook_add(g_uc, &hook_wp, UC_HOOK_MEM_WRITE,
                    (void *)(void (*)(uc_engine *, uc_mem_type, uint64_t,
                                      int, int64_t, void *))wp_cb,
                    nullptr, 0x50400000, 0x50500000);
    }

    // Select CPU model (matches KPX PVR 0x000c0000 = PowerPC 750 / G3).
    err = uc_ctl_set_cpu_model(g_uc, UC_CPU_PPC32_750_V3_1);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[Unicorn-PPC] uc_ctl_set_cpu_model(PPC32_750_V3_1) failed: %s\n",
                uc_strerror(err));
    }

    // ---- Memory map — mirror KPX's layout byte-for-byte --------------------
    // RAM + nanokernel probe pad (see src/core/cpu_context.cpp:init_ppc).
    const size_t NK_PROBE_PAD = 8 * 1024 * 1024;
    if (!map_region(g_uc, RAMBase, RAMSize + NK_PROBE_PAD,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    RAMBaseHost, "RAM+NKpad"))
        goto fail;

    // ROM area (5 MB at ROMBase). Matches host mmap (RW+X) — the nanokernel
    // patches ROM in place during init (e.g. at 0x503141cc), so RW is required.
    if (!map_region(g_uc, ROMBase, 0x500000,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    ROMBaseHost, "ROM"))
        goto fail;

    // KernelData aliases — single SHM, mapped at two Mac addresses.
    if (!map_region(g_uc, 0x68ffe000, 0x2000,
                    UC_PROT_READ | UC_PROT_WRITE,
                    (void *)(uintptr_t)0x68ffe000, "KD_hi"))
        goto fail;
    if (!map_region(g_uc, 0x5fffe000, 0x2000,
                    UC_PROT_READ | UC_PROT_WRITE,
                    (void *)(uintptr_t)0x5fffe000, "KD_lo"))
        goto fail;

    // DR Emulator / DR Cache — KPX `munmap`s these before nanokernel boot
    // (cpu_ppc_kpx.cpp:1204-1206) so probes hit SIGSEGV → skip instruction
    // → register stays untouched. Under Unicorn we leave them unmapped and
    // let the unmapped-memory hook auto-map zero pages on first touch,
    // matching KPX's "read returns 0, write is a no-op" shape. They're then
    // remapped to real RW RAM after the first IRQ (uppc_remap_dr_probes_once).

    // ---- Diagnostics: dump instructions around known divergence points ------
    {
        // Dump around 0x504ff224 — the current execute_fast stuck point.
        for (uint32_t a = 0x504ff200; a < 0x504ff280; a += 16) {
            uint32_t buf[4] = {0,0,0,0};
            if (uc_mem_read(g_uc, a, buf, sizeof(buf)) == UC_ERR_OK) {
                fprintf(stderr,
                    "[Unicorn-PPC] ROM @0x%08x: %08x %08x %08x %08x\n",
                    a, __builtin_bswap32(buf[0]), __builtin_bswap32(buf[1]),
                    __builtin_bswap32(buf[2]), __builtin_bswap32(buf[3]));
            }
        }
        // Dump the bctr-and-setup region that routes into zero-fill (0x5046xxx).
        fprintf(stderr, "[Unicorn-PPC] --- post-patch dump 0x50314200..0x50314260 ---\n");
        for (uint32_t a = 0x50314200; a < 0x50314260; a += 16) {
            uint32_t buf[4] = {0,0,0,0};
            if (uc_mem_read(g_uc, a, buf, sizeof(buf)) == UC_ERR_OK) {
                fprintf(stderr,
                    "[Unicorn-PPC] ROM @0x%08x: %08x %08x %08x %08x\n",
                    a, __builtin_bswap32(buf[0]), __builtin_bswap32(buf[1]),
                    __builtin_bswap32(buf[2]), __builtin_bswap32(buf[3]));
            }
        }
    }

    // Low-memory sanity check: the nanokernel at pc=0x503101fc executes
    //   lwz r1, 0x2804(r0)
    // to load its stack pointer from XLM_KERNEL_DATA. InitAll_PPC should have
    // stashed KernelDataAddr there via WriteMacInt32 BEFORE cpu_init runs.
    // If this reads 0 we know the write never reached Unicorn-visible memory
    // (either init ordering or host/guest-mapping mismatch).
    {
        uint32_t vals[8] = {0};
        uc_err r = uc_mem_read(g_uc, 0x2800, vals, sizeof(vals));
        fprintf(stderr, "[Unicorn-PPC] LOWMEM read @0x2800..0x2820 (%s):\n",
                uc_strerror(r));
        for (int i = 0; i < 8; ++i) {
            fprintf(stderr, "    0x%04x = 0x%08x (be 0x%08x)\n",
                    0x2800 + i*4, vals[i], __builtin_bswap32(vals[i]));
        }
        fprintf(stderr, "[Unicorn-PPC] Expected 0x2804 = KernelDataAddr = 0x%08x\n",
                KernelDataAddr);

        // Cross-check: read via the host pointer directly.
        uint32_t host_val = *(uint32_t *)(uintptr_t)0x2804;
        fprintf(stderr, "[Unicorn-PPC] HOST-DIRECT *0x2804 = 0x%08x (be 0x%08x)\n",
                host_val, __builtin_bswap32(host_val));

        // Dump KernelData+0x1180..0x11a0 before execution to see if the
        // "emulator init routine pointer" field is already set.
        uint32_t kd_vals[16] = {0};
        uc_err kr = uc_mem_read(g_uc, 0x68fff180, kd_vals, sizeof(kd_vals));
        fprintf(stderr, "[Unicorn-PPC] KernelData+0x1180..0x11c0 pre-exec (%s):\n",
                uc_strerror(kr));
        for (int i = 0; i < 16; i += 4) {
            fprintf(stderr, "    0x%08x = %08x %08x %08x %08x\n",
                    0x68fff180 + i*4,
                    __builtin_bswap32(kd_vals[i+0]),
                    __builtin_bswap32(kd_vals[i+1]),
                    __builtin_bswap32(kd_vals[i+2]),
                    __builtin_bswap32(kd_vals[i+3]));
        }
    }

    // ---- Initial CPU state — matches KPX's kpx_cpu_init --------------------
    {
        const uint32_t entry  = ROMBase + 0x310000;
        const uint32_t gpr3   = ROMBase + 0x30d000;
        const uint32_t gpr4   = KernelDataAddr + 0x1000;

        wr_pc(entry);
        wr_gpr(3, gpr3);
        wr_gpr(4, gpr4);

        uppc_mem_write_long(XLM_RUN_MODE, MODE_68K);

        fprintf(stderr, "[Unicorn-PPC] cpu_init: entry=0x%08x gpr3=0x%08x gpr4=0x%08x\n",
                entry, gpr3, gpr4);

        // Dump initial MSR — KPX boots with IR/DR=0 (real addressing). If
        // Unicorn ships a different default, translation-dependent paths in
        // the nanokernel fork off silently.
        uint32_t msr = 0;
        uc_reg_read(g_uc, UC_PPC_REG_MSR, &msr);
        fprintf(stderr, "[Unicorn-PPC] cpu_init: initial MSR=0x%08x (IR=%d DR=%d FP=%d PR=%d)\n",
                msr, !!(msr & 0x20), !!(msr & 0x10), !!(msr & 0x2000),
                !!(msr & 0x4000));

        // Enable MSR.FP (bit 13, mask 0x2000). KPX is a user-mode interpreter
        // with no real MSR, so FP instructions always execute; its ROM patches
        // (rom_patches_ppc.cpp:1336,1398) specifically remove the nanokernel's
        // "enable FPU" sequences because FP is assumed already on. Unicorn
        // raises UC_ERR_EXCEPTION (POWERPC_EXCP_FPU) on any `lfd`/`stfd`/etc
        // when MSR.FP=0 — e.g. the FP context-restore block at 0x50312e00.
        // Force MSR.FP=1 to match KPX's effective runtime shape.
        msr |= 0x2000u;
        uc_reg_write(g_uc, UC_PPC_REG_MSR, &msr);
        uc_reg_read(g_uc, UC_PPC_REG_MSR, &msr);
        fprintf(stderr, "[Unicorn-PPC] cpu_init:   after MSR=0x%08x (IR=%d DR=%d FP=%d PR=%d)\n",
                msr, !!(msr & 0x20), !!(msr & 0x10), !!(msr & 0x2000),
                !!(msr & 0x4000));
    }
    return true;

fail:
    uc_close(g_uc); g_uc = nullptr;
    return false;
}

static void uppc_cpu_reset(void)          {}
static void uppc_cpu_set_type(int, int)   {}
static int  uppc_cpu_execute_one(void)    { return 0; }

// ----- interrupt(entry) — ported from sheepshaver_cpu::interrupt -----------
// Invokes the nanokernel IRQ routine as a normal function using a fake stack
// and POWERPC_EXEC_RETURN sentinel as the return address. Matches KPX
// exactly; see docs/ppc/UnicornPpcPlan.md §2.
static void uppc_interrupt(uint32_t entry)
{
    if (!g_uc) return;

    uint32_t saved_pc  = rd_pc();
    uint32_t saved_lr  = rd_lr();
    uint32_t saved_ctr = rd_ctr();
    uint32_t saved_sp  = rd_gpr(1);

    wr_gpr(1, (uint32_t)SignalStackBase() - 64);

    uint32_t trampoline = POWERPC_EXEC_RETURN;  // set in LR after stash

    WriteMac32(KERNEL_DATA_BASE + 0x004, rd_gpr(1));
    WriteMac32(KERNEL_DATA_BASE + 0x018, rd_gpr(6));

    uint32_t ksave = ReadMac32(KERNEL_DATA_BASE + 0x65c);
    WriteMac32(ksave + 0x13c, rd_gpr(7));
    WriteMac32(ksave + 0x144, rd_gpr(8));
    WriteMac32(ksave + 0x14c, rd_gpr(9));
    WriteMac32(ksave + 0x154, rd_gpr(10));
    WriteMac32(ksave + 0x15c, rd_gpr(11));
    WriteMac32(ksave + 0x164, rd_gpr(12));
    WriteMac32(ksave + 0x16c, rd_gpr(13));

    wr_gpr(1, ppc::KernelDataAddr);
    wr_gpr(7, ReadMac32(KERNEL_DATA_BASE + 0x660));
    wr_gpr(8, 0);
    // Use LR as the EXEC_RETURN trampoline instead of a SheepMem address —
    // we can't easily build a SheepVar32 from here without KPX headers, and
    // LR-as-return-address is architecturally correct for blr-style return.
    wr_lr(trampoline);
    wr_gpr(10, trampoline);
    wr_gpr(12, trampoline);
    wr_gpr(13, rd_cr());
    wr_gpr(11, 0xf072);

    uint32_t cr = rd_cr();
    wr_cr((0xf072 & 0x0fff0000u) | (cr & ~0x0fff0000u));

    // Run nanokernel IRQ handler; returns when EXEC_RETURN fires.
    wr_pc(entry);
    uc_err err = uc_emu_start(g_uc, entry, 0, 0, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[Unicorn-PPC] interrupt(0x%08x) uc_emu_start err=%s pc=0x%08x\n",
                entry, uc_strerror(err), rd_pc());
    }

    wr_pc(saved_pc);
    wr_lr(saved_lr);
    wr_ctr(saved_ctr);
    wr_gpr(1, saved_sp);
}

// HandleInterrupt-equivalent — ported from cpu_ppc_kpx.cpp:890-948.
// Decides whether to flag the 68k emulator or invoke the nanokernel handler.
static void uppc_handle_interrupt(void)
{
    if ((int32_t)ReadMac32(XLM_IRQ_NEST) > 0)
        return;

    uint32_t mode = ReadMac32(XLM_RUN_MODE);

    switch (mode) {
    case MODE_68K:
        WriteMac16(ReadMac32(KERNEL_DATA_BASE + 0x67c), 1);
        wr_cr(rd_cr() | ReadMac32(KERNEL_DATA_BASE + 0x674));
        break;

    case MODE_NATIVE:
        if (rd_gpr(1) != ppc::KernelDataAddr) {
            WriteMac16(ReadMac32(KERNEL_DATA_BASE + 0x67c), 1);
            uint32_t kframe = ReadMac32(KERNEL_DATA_BASE + 0x658) + 0xdc;
            WriteMac32(kframe, ReadMac32(kframe) | ReadMac32(KERNEL_DATA_BASE + 0x674));

            // Disable nested IRQs while running nanokernel handler, then fire.
            WriteMac32(XLM_IRQ_NEST, 1);
            uint32_t entry = (ppc::ROMType == ROMTYPE_NEWWORLD)
                ? ppc::ROMBase + 0x312b1c
                : ppc::ROMBase + 0x312a3c;
            uppc_interrupt(entry);
            WriteMac32(XLM_IRQ_NEST, 0);
        }
        break;

    case MODE_EMUL_OP:
        // §5 MODE_EMUL_OP 68k level-1 re-entry — deferred to debug pass
        // (requires Execute68k proc template path which the KPX compat layer
        // provides but we can't call directly from here yet).
        break;
    }

    // First IRQ also unblocks the DR probe remap per §3.
    uppc_remap_dr_probes_once();
}

// ----- Tick thread — 60 Hz, mirrors KPX tick_func --------------------------

static void uppc_tick_thread(void)
{
    int tick_counter = 0;
    uint64_t start = GetTicks_usec();
    uint64_t next = start;

    while (g_tick_thread_running.load()) {
        const int period_us = 16625;
        next += period_us;
        int64_t delay = (int64_t)(next - GetTicks_usec());
        if (delay > 0) {
            struct timespec ts = {0, (long)(delay * 1000)};
            nanosleep(&ts, nullptr);
        } else if (delay < -period_us) {
            next = GetTicks_usec();
        }
        if (tick_inhibit) continue;

        if (++tick_counter > 60) {
            tick_counter = 0;
            WriteMac32(0x20c, (uint32_t)time(nullptr) + 0x7C25B080u);
        }

        if (ReadMac32(XLM_IRQ_NEST) == 0) {
            SetInterruptFlag(INTFLAG_VIA);
            g_pending_irq.store(true);
            if (g_uc) uc_emu_stop(g_uc);  // kick CPU thread out of uc_emu_start
        }

        extern Platform g_platform;
        if (g_platform.video_refresh)
            g_platform.video_refresh();
    }
}

// ----- execute_fast — the top-level run loop -------------------------------

static void uppc_cpu_execute_fast(void)
{
    if (!g_uc) {
        fprintf(stderr, "[Unicorn-PPC] execute_fast: engine not initialized\n");
        return;
    }
    g_stop_requested = false;

    // Inhibit ticks until nanokernel init clears it (OP_RESET clears).
    tick_inhibit = true;

    // Start 60Hz tick thread.
    g_tick_thread_running.store(true);
    std::thread ticker(uppc_tick_thread);

    uint32_t entry = rd_pc();
    fprintf(stderr, "[Unicorn-PPC] execute_fast: entry=0x%08x\n", entry);

    // Run until QUIT or parent requests stop. uc_emu_start returns on every
    // EXEC_RETURN / EmulOp / IRQ kick — we resume at current PC.
    //
    // UC_ERR_{READ,WRITE}_UNMAPPED are KPX-style probe faults: advance PC by 4
    // and resume, leaving destination register / memory unchanged. Only a
    // *true* stall (same PC hit 3+ times after a hard error) bails out.
    uint32_t stuck_pc = 0;
    int stuck_count = 0;
    uint64_t skipped = 0;
    while (!g_stop_requested) {
        uint32_t pc = rd_pc();
        uc_err err = uc_emu_start(g_uc, pc, 0, /*timeout*/0, /*count*/0);
        if (err == UC_ERR_READ_UNMAPPED || err == UC_ERR_WRITE_UNMAPPED) {
            // KPX SIGSEGV-skip shape: step past the dead instruction and keep
            // going. Log the first few per-PC so probe loops don't flood the
            // terminal but genuine bugs still surface.
            uint32_t after = rd_pc();
            static std::unordered_map<uint32_t, uint64_t> skip_counts;
            uint64_t &c = skip_counts[after];
            if (c < 3) {
                fprintf(stderr, "[Unicorn-PPC] skip %s @pc=0x%08x (n=%llu)\n",
                        (err == UC_ERR_READ_UNMAPPED) ? "READ" : "WRITE",
                        after, (unsigned long long)(c + 1));
            }
            c++; skipped++;
            if (!uppc_skip_memop_at(after)) {
                fprintf(stderr, "[Unicorn-PPC] unknown memop at pc=0x%08x — bailing\n", after);
                break;
            }
            stuck_count = 0;
            continue;
        }
        if (err != UC_ERR_OK) {
            uint32_t after = rd_pc();
            fprintf(stderr, "[Unicorn-PPC] uc_emu_start err=%s pc=0x%08x\n",
                    uc_strerror(err), after);
            if (after == stuck_pc) {
                if (++stuck_count >= 3) {
                    fprintf(stderr, "[Unicorn-PPC] stuck at pc=0x%08x — bailing out (skipped=%llu)\n",
                            after, (unsigned long long)skipped);
                    break;
                }
            } else {
                stuck_pc = after;
                stuck_count = 1;
            }
            if (g_stop_requested) break;
            struct timespec ts = {0, 1000000}; // 1 ms
            nanosleep(&ts, nullptr);
        } else {
            stuck_count = 0;
        }

        if (g_pending_irq.exchange(false)) {
            uppc_handle_interrupt();
        }
    }

    g_tick_thread_running.store(false);
    if (ticker.joinable()) ticker.join();

    fprintf(stderr, "[Unicorn-PPC] execute_fast: exiting (emulops=%llu pc=0x%08x)\n",
            (unsigned long long)g_emulop_count, rd_pc());
}

static void uppc_cpu_request_stop(void)
{
    g_stop_requested = true;
    if (g_uc) uc_emu_stop(g_uc);
}

static void uppc_cpu_trigger_interrupt(int /*level*/)
{
    g_pending_irq.store(true);
    if (g_uc) uc_emu_stop(g_uc);
}

// ----- execute_ppc — run native PPC from host context ----------------------
// Mirrors sheepshaver_cpu::execute_ppc. Sets LR to the EXEC_RETURN sentinel
// so a blr from `entry` unwinds us cleanly.
static void uppc_cpu_execute_ppc(uint32_t entry)
{
    if (!g_uc) {
        fprintf(stderr, "[Unicorn-PPC] execute_ppc(0x%08x): engine null\n", entry);
        return;
    }

    uint32_t saved_pc = rd_pc();
    uint32_t saved_lr = rd_lr();
    wr_lr(POWERPC_EXEC_RETURN);
    wr_pc(entry);

    // Skip-on-unmapped loop mirrors execute_fast: KPX's SIGSEGV handler
    // advances PC past a dead load/store and resumes. Unicorn surfaces
    // UC_ERR_{READ,WRITE}_UNMAPPED — we skip and retry so nested callouts
    // (FindLibSymbol, 68k trap procs) don't abandon mid-flight with a
    // half-restored r1. The loop terminates via uc_emu_stop from the
    // EXEC_RETURN EmulOp handler, or on a true stall / fetch fault.
    uint32_t run_pc = entry;
    uint32_t stuck_pc = 0;
    int stuck_count = 0;
    for (;;) {
        uc_err err = uc_emu_start(g_uc, run_pc, 0, 0, 0);
        if (err == UC_ERR_OK) break;
        if (err == UC_ERR_READ_UNMAPPED || err == UC_ERR_WRITE_UNMAPPED) {
            uint32_t after = rd_pc();
            if (!uppc_skip_memop_at(after)) {
                fprintf(stderr, "[Unicorn-PPC] execute_ppc(0x%08x) unknown memop @pc=0x%08x — bailing\n",
                        entry, after);
                break;
            }
            run_pc = rd_pc();
            stuck_count = 0;
            continue;
        }
        uint32_t after = rd_pc();
        fprintf(stderr, "[Unicorn-PPC] execute_ppc(0x%08x) err=%s pc=0x%08x\n",
                entry, uc_strerror(err), after);
        if (after == stuck_pc && ++stuck_count >= 3) break;
        if (after != stuck_pc) { stuck_pc = after; stuck_count = 1; }
        run_pc = after;
        if (g_stop_requested) break;
    }

    wr_pc(saved_pc);
    wr_lr(saved_lr);
}

// ----- execute_68k — port of sheepshaver_cpu::execute_68k ------------------
// Enters the 68k emulator from a PPC EMUL_OP context by setting up the
// nanokernel's 68k-exec GPR layout and jumping into the 68k opcode table.
static void uppc_cpu_execute_68k(uint32_t entry, struct M68kRegisters *r)
{
    if (!g_uc || !r) return;

    uint32_t saved_pc  = rd_pc();
    uint32_t saved_lr  = rd_lr();
    uint32_t saved_ctr = rd_ctr();
    uint32_t saved_cr  = rd_cr();

    // 56-byte MacOS stack frame.
    uint32_t sp = rd_gpr(1);
    wr_gpr(1, sp - 56);
    WriteMac32(sp - 56, sp);

    // Save PPC non-volatile GPRs (13..31).
    uint32_t saved_gprs[19];
    for (int i = 0; i < 19; i++) saved_gprs[i] = rd_gpr(13 + i);

    // Supervisor bit in CR[SO] of field 0.
    wr_cr(0x10000000u);

    for (int i = 0; i < 8; i++) wr_gpr(8 + i,  r->d[i]);
    for (int i = 0; i < 7; i++) wr_gpr(16 + i, r->a[i]);
    wr_gpr(23, 0);
    wr_gpr(24, entry);
    wr_gpr(25, ReadMac32(XLM_68K_R25));
    wr_gpr(26, 0);
    wr_gpr(28, 0);
    wr_gpr(29, ReadMac32(KERNEL_DATA_BASE + 0x1074));
    wr_gpr(30, ReadMac32(KERNEL_DATA_BASE + 0x1078));
    wr_gpr(31, ppc::KernelDataAddr + 0x1000);

    // Push EXEC_RETURN opcode address on stack as the return path.
    uint32_t new_sp = rd_gpr(1) - 4;
    wr_gpr(1, new_sp);
    WriteMac32(new_sp, XLM_EXEC_RETURN_OPCODE);

    WriteMac32(XLM_RUN_MODE, MODE_68K);
    wr_gpr(0, 0);

    // Decode first 68k opcode + index into opcode table.
    uint32_t g24 = rd_gpr(24);
    uint32_t opcode = ReadMac16(g24);
    uint32_t g27 = (int32_t)(int16_t)ReadMac16(g24 + 2);
    wr_gpr(27, g27);
    wr_gpr(24, g24 + 2);
    wr_gpr(29, rd_gpr(29) + opcode * 8);

    uppc_cpu_execute_ppc(rd_gpr(29));

    WriteMac32(XLM_68K_R25, rd_gpr(25));
    WriteMac32(XLM_RUN_MODE, MODE_EMUL_OP);

    for (int i = 0; i < 8; i++) r->d[i] = rd_gpr(8 + i);
    for (int i = 0; i < 7; i++) r->a[i] = rd_gpr(16 + i);

    for (int i = 0; i < 19; i++) wr_gpr(13 + i, saved_gprs[i]);
    wr_gpr(1, rd_gpr(1) + 56);

    wr_pc(saved_pc);
    wr_lr(saved_lr);
    wr_ctr(saved_ctr);
    wr_cr(saved_cr);
}

static void uppc_cpu_execute_68k_trap(uint16_t trap, struct M68kRegisters *r)
{
    // Construct a tiny procedure in SheepMem-equivalent scratch: since we
    // can't easily allocate SheepMem from here, we write to ScratchMem via a
    // known fixed slot. KPX uses SheepVar (dynamic alloc). For now, stash
    // at a dedicated slot in the XLM region; safe because nothing else uses it
    // during trap dispatch.
    static constexpr uint32_t TRAP_PROC = 0x68ffec30u;  // 4 bytes scratch
    WriteMac16(TRAP_PROC, trap);
    WriteMac16(TRAP_PROC + 2, 0x4e75u);  // M68K_RTS
    uppc_cpu_execute_68k(TRAP_PROC, r);
}

// ----- State query ---------------------------------------------------------

static uint32_t uppc_cpu_get_pc(void)  { return rd_pc(); }
static uint16_t uppc_cpu_get_sr(void)  { return 0; }
static uint32_t uppc_cpu_get_dreg(int) { return 0; }
static uint32_t uppc_cpu_get_areg(int) { return 0; }

static uint32_t uppc_cpu_get_gpr(int n)
{
    if (n < 0 || n >= 32) return 0;
    return rd_gpr(n);
}
static void uppc_cpu_set_gpr(int n, uint32_t val)
{
    if (n < 0 || n >= 32) return;
    wr_gpr(n, val);
}
static uint32_t uppc_cpu_get_cr(void)  { return rd_cr(); }
static uint32_t uppc_cpu_get_lr(void)  { return rd_lr(); }
static uint32_t uppc_cpu_get_ctr(void) { return rd_ctr(); }

static void uppc_invoke_debug(void) {}
static void uppc_flush_code_cache(void)
{
    if (g_uc) uc_emu_stop(g_uc);
}

// make_emulop — the legacy m68k backend uses this to form 0xAExx trap words;
// PPC uses the helper-based dispatch so we just echo the selector for any
// callers that still poke this path.
static uint16_t uppc_make_emulop(uint16_t op) { return op; }

// Route EMUL_OP dispatch to the real KPX-shared handler. The previous stub
// silently clobbered g_platform.ppc_emulop_handler so OP_CHECKLOAD and
// friends ran as no-ops — leaving A7 un-cleaned and the next RTS popping the
// stray parameter as a PC. Delegate to ppc::EmulOp (the same function
// kpx_ppc_emulop_handler uses in the KPX backend).
static void uppc_ppc_emulop_handler(void *r68k_regs, uint32_t pc, int selector)
{
    ppc::EmulOp(static_cast<M68kRegisters *>(r68k_regs), pc, selector);
}
static void uppc_ppc_cursor_move(uint32_t /*mouse_base*/, int /*x*/, int /*y*/)
{
    // Cursor update requires Execute68k of CursorDeviceDispatch — deferred to
    // the debug pass; stub here avoids crashing on cursor hooks during boot.
}

// ----- Install -------------------------------------------------------------

void cpu_unicorn_ppc_install(Platform *p)
{
    p->cpu_name = "Unicorn-PPC";
    p->use_aline_emulops = false;

    p->cpu_init = uppc_cpu_init;
    p->cpu_reset = uppc_cpu_reset;
    p->cpu_set_type = uppc_cpu_set_type;

    p->cpu_execute_one = uppc_cpu_execute_one;
    p->cpu_execute_fast = uppc_cpu_execute_fast;
    p->cpu_request_stop = uppc_cpu_request_stop;

    p->cpu_get_pc = uppc_cpu_get_pc;
    p->cpu_get_sr = uppc_cpu_get_sr;
    p->cpu_get_dreg = uppc_cpu_get_dreg;
    p->cpu_get_areg = uppc_cpu_get_areg;

    p->cpu_get_gpr = uppc_cpu_get_gpr;
    p->cpu_set_gpr = uppc_cpu_set_gpr;
    p->cpu_get_cr = uppc_cpu_get_cr;
    p->cpu_get_lr = uppc_cpu_get_lr;
    p->cpu_get_ctr = uppc_cpu_get_ctr;
    p->cpu_execute_ppc = uppc_cpu_execute_ppc;

    p->cpu_trigger_interrupt = uppc_cpu_trigger_interrupt;
    p->invoke_debug = uppc_invoke_debug;

    p->cpu_execute_68k_trap = uppc_cpu_execute_68k_trap;
    p->cpu_execute_68k = uppc_cpu_execute_68k;

    p->flush_code_cache = uppc_flush_code_cache;

    p->mem_read_byte = uppc_mem_read_byte;
    p->mem_read_word = uppc_mem_read_word;
    p->mem_read_long = uppc_mem_read_long;
    p->mem_write_byte = uppc_mem_write_byte;
    p->mem_write_word = uppc_mem_write_word;
    p->mem_write_long = uppc_mem_write_long;
    p->mem_mac_to_host = uppc_mem_mac_to_host;
    p->mem_host_to_mac = uppc_mem_host_to_mac;

    p->make_emulop = uppc_make_emulop;
    p->m68k_emulop_handler = nullptr;
    p->ppc_emulop_handler = uppc_ppc_emulop_handler;
    p->trap_handler = nullptr;
    p->ppc_cursor_move = uppc_ppc_cursor_move;
}

} // extern "C"

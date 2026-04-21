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
#include "ppc_boundary_trace.h"

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

// Framebuffer accessors — defined in src/cpu/kpx/video_ppc.cpp. Needed so we
// can map the framebuffer region into Unicorn's guest memory (lives above
// SheepMem, outside the SheepMem mapping).
extern "C" uint8_t *video_ppc_get_framebuffer_host();
extern "C" uint32_t video_ppc_get_framebuffer_size();
namespace ppc { extern uint32_t screen_base; }  // Mac address, set by VideoInit

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

// Native helpers invoked by EmulOp. EmulOp itself is dispatched via
// g_platform.ppc_emulop_handler; EXEC_NATIVE selectors route through
// g_platform.ppc_native_op (see KPX's kpx_ppc_native_op).
struct M68kRegisters;
namespace ppc {
    extern void EmulOp(M68kRegisters *r, uint32_t pc, int selector);
}

// Backend-neutral SheepMem reserve wrapper exposed by kpx/ppc_memory.cpp. Used
// from cpu_init to pre-allocate the execute_macos_code EXEC_RETURN trampoline.
extern "C" uint32_t kpx_sheep_mem_reserve(uint32_t sz);

// EXEC_RETURN trampoline slot — 4 bytes of SheepMem containing
// POWERPC_EXEC_RETURN as a PPC instruction. Allocated once at cpu_init so it
// sits at the top of SheepMem and isn't recycled by nested SheepVars.
static uint32_t g_macos_trampoline = 0;

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

// EmulOp selectors we care about for SMC flush (host-side writes to guest RAM).
// Values mirror the KPX OP_* enum in src/cpu/kpx/compat/emul_op.h — kept local
// to avoid the KPX-vs-common header conflict that macos_util.h guards against.
#define UPPC_OP_SONY_PRIME    11
#define UPPC_OP_DISK_PRIME    15
#define UPPC_OP_CDROM_PRIME   19
#define UPPC_OP_SOUNDIN_PRIME 24
#define UPPC_OP_GET_SCRAP     35
#define UPPC_OP_EXTFS_COMM    49
#define UPPC_OP_EXTFS_HFS     50

#define KERNEL_DATA_BASE      0x68ffe000u

// DR probe regions — munmap'd during init so nanokernel faults, then remapped
// as RW after first IRQ (see §3 of plan).
#define DR_EMUL_BASE   0x68070000u
#define DR_EMUL_SIZE   0x00010000u
#define DR_CACHE_BASE  0x69000000u
#define DR_CACHE_SIZE  0x00080000u

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
// Nesting depth of uc_emu_start. Incremented by the outer execute_fast loop
// and by the recursive execute_ppc / execute_68k callouts. The cooperative
// IRQ-stop in uppc_mac_emulop_cb must only fire at the outermost level —
// otherwise a pending tick interrupts a nested Execute68k (e.g. FindLibSymbol's
// GetSharedLibrary → CFMDispatch trap proc) mid-dispatch, uc_emu_stop unwinds
// before EXEC_RETURN, and the caller reads back corrupted D0/A-regs.
static int g_emu_nest_depth = 0;
// Set when the EXEC_RETURN EmulOp fires; consumed by nested execute_ppc to
// distinguish a clean return ("function done") from a spurious uc_emu_stop
// (tick thread IRQ kick that must not unwind the caller). Without this,
// tick-thread stops while nested abandon Execute68k mid-dispatch.
static bool g_exec_return_seen = false;
static std::atomic<bool> g_tick_thread_running{false};
static bool g_dr_probes_remapped = false;
static uint64_t g_emulop_count = 0;

// Always-on last-block tracker. A wildcard UC_HOOK_BLOCK updates a 32-entry
// ring with each TB's entry PC; the crash handler reads these globals to
// report which guest PCs were executing just before a SIGABRT / SIGSEGV.
// Linkage is C so the crash handler (which doesn't know about C++ name
// mangling) can weak-extern them.
extern "C" {
    volatile uint32_t  g_uppc_last_block_pc       = 0;
    uint32_t           g_uppc_last_block_pcs[32]  = {0};
    volatile int       g_uppc_last_block_pcs_idx  = 0;
    volatile uint64_t  g_uppc_block_seq           = 0;

    // 68k-PC ring populated only when MACEMU_PPC_TRACE_68K_ENTRY is armed.
    // Each PPC block in ROM writes current r24 to this ring so the entry-cb
    // can dump "recent 68k PCs" and we can see what 68k control flow led into
    // a wild-jump target. Dedups adjacent identical values to compress bursts
    // where one 68k instruction spans many PPC blocks.
    uint32_t           g_uppc_last_r24s[128]      = {0};
    int                g_uppc_last_r24s_idx       = 0;
}

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
    // Zero the destination register on a failed load. This breaks dispatch-
    // table probe loops that would otherwise spin forever on a preserved
    // register value (e.g. 0x504a73ac: `lwz r24, 0(r1); b 0x50467da0` which
    // re-enters with the same r24, branches to the same handler, loops).
    // With rd=0 the dispatch takes the "null" path which eventually exits.
    // KPX's SIGSEGV-skip on x86 leaves RAX at whatever the just-missed
    // `mov (%r1), %rax` would have set it to; in practice that's often 0
    // because the register was freshly loaded. Zeroing matches that common
    // case without relying on host-register side effects.
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

// Monotonic count of EmulOps dispatched, shared with the CR-trace hook so we
// can narrow per-instruction logging to a specific EmulOp range.
static uint64_t g_emul_op_seq = 0;

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

    ++g_emul_op_seq;
    {
        PpcBoundaryState ts;
        ts.pc = pc;
        ts.selector = emul_op;
        ts.cr = rd_cr();
        ts.xer = saved_xer;
        ts.lr = rd_lr();
        ts.ctr = rd_ctr();
        for (int i = 0; i < 32; ++i) ts.gpr[i] = rd_gpr(i);
        ppc_trace_emul_op(ts);
    }

    g_platform.ppc_emulop_handler(&r68, rd_gpr(24), emul_op);

    // Host-side writes to guest RAM (disk/CD/ExtFS reads; scrap/audio buffers)
    // bypass TCG's TLB, so the in-TB check for TLB_NOTDIRTY never fires and
    // stale translations can outlive an overwritten code page. Flush the TB
    // cache after EmulOps known to memcpy into guest space. Cheap relative to
    // the I/O they follow; avoids the ~9x slowdown of per-iteration flush.
    switch (emul_op) {
        case UPPC_OP_SONY_PRIME:
        case UPPC_OP_DISK_PRIME:
        case UPPC_OP_CDROM_PRIME:
        case UPPC_OP_SOUNDIN_PRIME:
        case UPPC_OP_EXTFS_COMM:
        case UPPC_OP_EXTFS_HFS:
        case UPPC_OP_GET_SCRAP:
            uc_ctl_flush_tb(g_uc);
            break;
        default:
            break;
    }

    wr_cr(saved_cr);
    wr_xer(saved_xer);

    for (int i = 0; i < 8; i++)
        wr_gpr(8 + i, r68.d[i]);
    for (int i = 0; i < 7; i++)
        wr_gpr(16 + i, r68.a[i]);
    wr_gpr(1, r68.a[7]);

    WriteMac32(XLM_RUN_MODE, MODE_68K);

    {
        PpcBoundaryState ts;
        ts.pc = pc;
        ts.selector = emul_op;
        ts.cr = rd_cr();
        ts.xer = rd_xer();
        ts.lr = rd_lr();
        ts.ctr = rd_ctr();
        for (int i = 0; i < 32; ++i) ts.gpr[i] = rd_gpr(i);
        ppc_trace_emul_op_post(ts);
    }
}

// EXEC_NATIVE — dispatch one native-op selector through the Platform API.
// The handlers (NQD_*, Serial*, AO_*, ether_*, VideoDoDriverIO,
// check_load_invoc, MakeExecutable, etc.) are pure functions that live in KPX
// but operate on a caller-supplied uint32[32] GPR array. We marshal r0..r31
// into a flat array, invoke g_platform.ppc_native_op, and marshal the (few)
// mutated entries back into Unicorn's register file.
static void uppc_dispatch_native_op(uint32_t pc, uint32_t opcode)
{
    uint32_t selector = (opcode >> 6) & 0x3F;
    bool return_via_lr = (opcode >> 12) & 1;

    static const bool s_trace_native = []() {
        const char* e = std::getenv("MACEMU_PPC_TRACE_TRAP");
        return e && *e && *e != '0';
    }();
    if (s_trace_native) {
        fprintf(stderr, "[Unicorn-PPC]     EXEC_NATIVE selector=%u fn=%d @pc=0x%08x lr=0x%08x\n",
                selector, (int)return_via_lr, pc, rd_lr());
    }

    if (g_platform.ppc_native_op) {
        uint32_t gprs[32];
        for (int i = 0; i < 32; ++i) gprs[i] = rd_gpr(i);
        g_platform.ppc_native_op(selector, gprs);
        for (int i = 0; i < 32; ++i) wr_gpr(i, gprs[i]);
    } else {
        fprintf(stderr, "[Unicorn-PPC] EXEC_NATIVE selector=%u: no ppc_native_op registered @pc=0x%08x\n",
                selector, pc);
    }

    if (s_trace_native) {
        fprintf(stderr, "[Unicorn-PPC]     EXEC_NATIVE selector=%u done\n", selector);
    }

    if (return_via_lr)
        wr_pc(rd_lr());
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
        g_exec_return_seen = true;
        uc_emu_stop(uc);
        return;

    case 2: // EXEC_NATIVE
        uppc_dispatch_native_op(pc, opcode);
        if (g_emu_nest_depth <= 1 && g_pending_irq.load()) uc_emu_stop(uc);
        return;

    default: // EMUL_OP (selector = (opcode & 0x3f) - 3)
        uppc_dispatch_emul_op(pc, opcode);
        if (g_emu_nest_depth <= 1 && g_pending_irq.load()) uc_emu_stop(uc);
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

    // CR-trace hook: gated by MACEMU_PPC_CR2_TRACE=<start>[:<end>] (EmulOp seq
    // numbers). Inside the window, dumps pc/opcode/cr/lr/r24 on every
    // instruction firing. Uses the shared ppc_trace_cr_step() helper so the
    // output format matches KPX byte-for-byte and the two logs can be diffed
    // mechanically.
    if (ppc_cr_trace_window_().enabled) {
        static auto cr_cb = [](uc_engine *uc, uint64_t addr, uint32_t, void *) {
            if (!ppc_cr_trace_active_()) return;
            uint32_t cr = 0, lr = 0, r24 = 0;
            uc_reg_read(uc, UC_PPC_REG_CR, &cr);
            uc_reg_read(uc, UC_PPC_REG_LR, &lr);
            uc_reg_read(uc, UC_PPC_REG_24, &r24);
            uint32_t op_be = 0;
            uc_mem_read(uc, addr, &op_be, 4);
            uint32_t op = __builtin_bswap32(op_be);
            ppc_trace_cr_step((uint32_t)addr, op, cr, lr, r24);
        };
        uc_hook hh = 0;
        uc_hook_add(g_uc, &hh, UC_HOOK_CODE,
                    (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))cr_cb,
                    nullptr, 0, 0xffffffffull);
        const auto& w = ppc_cr_trace_window_();
        fprintf(stderr, "[Unicorn-PPC] CR tracer enabled for EmulOp seq [%llu,%llu)\n",
                (unsigned long long)w.lo, (unsigned long long)w.hi);
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
            // One-shot: on the VERY first unmapped access (of any kind), dump
            // the block-PC ring + insns around LR. This captures the caller
            // chain that led to the corrupt pointer — useful because the
            // corruption value varies run-to-run (seen: 0xc0ff..., 0x48f9...,
            // 0x4a6f... — all unmapped, different root pointer).
            {
                static bool dumped = false;
                if (!dumped) {
                    dumped = true;
                    uint32_t pc = 0, lr = 0, insn_be = 0;
                    uc_reg_read(uc, UC_PPC_REG_PC, &pc);
                    uc_reg_read(uc, UC_PPC_REG_LR, &lr);
                    uc_mem_read(uc, pc, &insn_be, 4);
                    uint32_t insn = __builtin_bswap32(insn_be);
                    fprintf(stderr,
                            "[Unicorn-PPC] *** first unmapped access: "
                            "pc=%08x insn=%08x target=0x%010llx lr=%08x ***\n",
                            pc, insn, (unsigned long long)addr, lr);
                    fprintf(stderr, "  recent block-PCs (ring; newest at idx=%d):\n   ",
                            g_uppc_last_block_pcs_idx);
                    for (int i = 0; i < 32; ++i) {
                        fprintf(stderr, " %08x", g_uppc_last_block_pcs[i]);
                        if (i == 15) fprintf(stderr, "\n   ");
                    }
                    fprintf(stderr, "\n");
                    fprintf(stderr, "  --- insns @ lr-0x20 .. lr+0x10 ---\n");
                    for (int off = -0x20; off <= 0x10; off += 4) {
                        uint32_t ins = 0, at = lr + off;
                        if (uc_mem_read(uc, at, &ins, 4) == UC_ERR_OK) {
                            ins = __builtin_bswap32(ins);
                            fprintf(stderr, "    %08x: %08x%s\n", at, ins,
                                    off == 0 ? "  <== lr" : "");
                        }
                    }
                }
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

        // IRQ-delivery hook: always-on UC_HOOK_BLOCK that checks g_pending_irq
        // at each TB boundary and exits cleanly via uc_emu_stop when set.
        // This replaces the cross-thread async uc_emu_stop from the tick
        // thread, which was torpedoing throughput at SCALE=1 (~50×
        // wall-clock collapse): the async stop tears the current TB
        // mid-execution with full state unwind, while a block-boundary stop
        // is a cheap exit_request flag check at a natural TB exit point.
        // Gate on g_emu_nest_depth <= 1 mirrors the old tick-thread guard —
        // don't interrupt nested execute_ppc / NK handlers. Opt out via
        // MACEMU_PPC_NO_IRQ_HOOK=1 if this regresses anything.
        static const bool s_no_irq_hook = [](){
            const char* e = std::getenv("MACEMU_PPC_NO_IRQ_HOOK");
            return e && *e && *e != '0';
        }();
        if (!s_no_irq_hook) {
            static auto irq_check_cb = [](uc_engine *uc, uint64_t, uint32_t, void *) {
                if (g_pending_irq.load(std::memory_order_relaxed) &&
                    g_emu_nest_depth <= 1) {
                    uc_emu_stop(uc);
                }
            };
            uc_hook hook_irq_check = 0;
            uc_hook_add(g_uc, &hook_irq_check, UC_HOOK_BLOCK,
                        (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))irq_check_cb,
                        nullptr, 1, 0);
        }

        // Last-block-PC tracker. Updates a 32-slot ring + counter per TB so
        // the crash handler can report "what guest PC were we at?" when
        // QEMU internals abort. Opt-in via MACEMU_PPC_BLOCK_TRACE=1 —
        // measured 6.58% wall on a 20s headless boot (late-9b profile),
        // not the ~2% the old comment predicted. The hook-dispatch
        // machinery, not the bookkeeping, is the cost.
        static auto last_pc_cb = [](uc_engine *, uint64_t addr,
                                    uint32_t, void *) {
            uint32_t pc = (uint32_t)addr;
            g_uppc_last_block_pc = pc;
            int i = g_uppc_last_block_pcs_idx;
            g_uppc_last_block_pcs[i & 31] = pc;
            g_uppc_last_block_pcs_idx = (i + 1) & 31;
            g_uppc_block_seq++;
        };
        {
            const char* bt = std::getenv("MACEMU_PPC_BLOCK_TRACE");
            const bool enabled = bt && *bt && *bt != '0';
            if (enabled) {
                uc_hook hook_last_pc = 0;
                uc_hook_add(g_uc, &hook_last_pc, UC_HOOK_BLOCK,
                            (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))last_pc_cb,
                            nullptr, 1, 0);
            }
        }

        // 68k PC entry tracer: MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...]
        // Fires once per PPC block across ROM; when r24 (68k PC) matches a
        // target, dumps A0..A7/D0..D7 and memory at A1 (for JMP(A1)-return
        // routines) and A7/SP. Built for hunting the caller of 0x500c6198
        // (PStrToCStr). Every hit is logged (unlike PC-DUMP's first-only).
        static uint32_t g_trace_68k_pcs[8] = {0};
        static int g_trace_68k_pc_n = 0;
        if (const char* tp = std::getenv("MACEMU_PPC_TRACE_68K_ENTRY"); tp && *tp) {
            char buf[256];
            std::strncpy(buf, tp, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            char *saveptr = nullptr;
            for (char *tok = strtok_r(buf, ",", &saveptr);
                 tok && g_trace_68k_pc_n < 8;
                 tok = strtok_r(nullptr, ",", &saveptr)) {
                uint32_t pc = (uint32_t)std::strtoul(tok, nullptr, 0);
                if (!pc) continue;
                g_trace_68k_pcs[g_trace_68k_pc_n++] = pc;
                fprintf(stderr, "[Unicorn-PPC] 68K-ENTRY armed on r24=0x%08x\n", pc);
            }
            static auto entry_cb = [](uc_engine *uc, uint64_t ppc_pc,
                                      uint32_t, void *) {
                uint32_t r24 = 0;
                uc_reg_read(uc, UC_PPC_REG_24, &r24);
                bool match = false;
                for (int i = 0; i < g_trace_68k_pc_n; i++) {
                    if (g_trace_68k_pcs[i] == r24) { match = true; break; }
                }
                if (!match) return;
                uint32_t gpr[32] = {0};
                for (int i = 0; i < 32; i++)
                    uc_reg_read(uc, UC_PPC_REG_0 + i, &gpr[i]);
                static uint32_t hit = 0;
                hit++;
                fprintf(stderr, "[68K-ENTRY #%u] r24=0x%08x ppc_pc=0x%08llx\n",
                        hit, r24, (unsigned long long)ppc_pc);
                fprintf(stderr, "  [68k D0..D7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                        gpr[8], gpr[9], gpr[10], gpr[11],
                        gpr[12], gpr[13], gpr[14], gpr[15]);
                fprintf(stderr, "  [68k A0..A7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                        gpr[16], gpr[17], gpr[18], gpr[19],
                        gpr[20], gpr[21], gpr[22], gpr[23]);
                // Peek 16 bytes @ A1 (return target for JMP(A1) convention)
                uint8_t mem[32];
                if (gpr[17] && uc_mem_read(uc, gpr[17], mem, 16) == UC_ERR_OK) {
                    fprintf(stderr, "  [mem @A1=0x%08x]", gpr[17]);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", mem[i]);
                    fprintf(stderr, "\n");
                }
                // Peek 16 bytes @ A7/SP (return addr if BSR/JSR caller)
                if (gpr[23] && uc_mem_read(uc, gpr[23], mem, 16) == UC_ERR_OK) {
                    fprintf(stderr, "  [mem @A7=0x%08x]", gpr[23]);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", mem[i]);
                    fprintf(stderr, "\n");
                }
                // Recent block PCs from the ring
                fprintf(stderr, "  recent PPC blocks: ");
                int idx = g_uppc_last_block_pcs_idx;
                for (int i = 0; i < 16; i++) {
                    int j = (idx - 16 + i) & 31;
                    fprintf(stderr, "%08x ", g_uppc_last_block_pcs[j]);
                }
                fprintf(stderr, "\n");
                // Recent 68k PCs (dedup-adjacent) — shows 68k control flow
                // path that led into this match. Useful for wild-jump diag.
                fprintf(stderr, "  recent 68k PCs: ");
                int ridx = g_uppc_last_r24s_idx;
                for (int i = 0; i < 128; i++) {
                    int j = (ridx - 128 + i) & 127;
                    fprintf(stderr, "%08x ", g_uppc_last_r24s[j]);
                }
                fprintf(stderr, "\n");
            };
            uc_hook hook_entry = 0;
            uc_hook_add(g_uc, &hook_entry, UC_HOOK_BLOCK,
                        (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))entry_cb,
                        nullptr, ROMBase, ROMBase + 0x500000);

            // Secondary always-on (for this run) block cb: record r24 per PPC
            // block across ROM into g_uppc_last_r24s ring. Dedup-adjacent so
            // repeated blocks of the same 68k instruction don't flood the
            // ring. Enables "recent 68k PCs" dump above.
            static auto r24_ring_cb = [](uc_engine *uc, uint64_t, uint32_t,
                                         void *) {
                uint32_t r24 = 0;
                uc_reg_read(uc, UC_PPC_REG_24, &r24);
                int i = g_uppc_last_r24s_idx;
                int prev = (i - 1) & 127;
                if (g_uppc_last_r24s[prev] == r24) return;  // dedup
                g_uppc_last_r24s[i & 127] = r24;
                g_uppc_last_r24s_idx = (i + 1) & 127;
            };
            uc_hook hook_r24_ring = 0;
            uc_hook_add(g_uc, &hook_r24_ring, UC_HOOK_BLOCK,
                        (void *)(void (*)(uc_engine *, uint64_t, uint32_t, void *))r24_ring_cb,
                        nullptr, ROMBase, ROMBase + 0x500000);
        }
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

    // Signal stack — 64KB at ROMEnd (0x50500000..0x50510000). Nanokernel IRQ
    // entry switches to this stack via KPX kpx_set_signal_stack(ROMEnd). Without
    // mapping it, the first stw at r1+offset faults UC_ERR_WRITE_UNMAPPED, the
    // unmapped-skip handler advances past it but corrupts the NK's saved-state
    // push, and subsequent interrupts wedge on the half-built frame.
    // Host mapped this as part of the same mmap() in cpu_context.cpp (ROM area
    // size = ROM_AREA_SIZE + SIG_STACK_SIZE), so the pages already exist.
    if (!map_region(g_uc, ROMBase + 0x500000, 0x10000,
                    UC_PROT_READ | UC_PROT_WRITE,
                    ROMBaseHost + 0x500000, "SigStack"))
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

    // SheepMem — host-allocated 512KB region at MAP_BASE (typically
    // 0x10000000) holding SheepVar-allocated 68k procedures and scratch
    // buffers. Without this mapping, any Execute68k() call into a SheepMem
    // proc (e.g. macos_util_ppc.cpp's GetSharedLibrary trampoline) loops
    // forever in the unmapped-skip handler. SheepMem::Init runs earlier in
    // init_ppc, so SheepMem_base is valid by the time we get here.
    {
        extern uintptr_t SheepMem_base;
        constexpr size_t SHEEPMEM_SIZE = 0x80000;  // matches SheepMem::size
        if (SheepMem_base == 0) {
            fprintf(stderr, "[Unicorn-PPC] WARNING: SheepMem_base unset — "
                            "Execute68k() into SheepMem procs will fail\n");
        } else if (!map_region(g_uc, (uint64_t)SheepMem_base, SHEEPMEM_SIZE,
                               UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                               (void *)SheepMem_base, "SheepMem")) {
            goto fail;
        }
    }

    // Framebuffer — vm_acquire'd in VideoInit (called from InitAll_PPC, which
    // ran in step 7 of cpu_context.cpp:init_ppc, before this cpu_init hook).
    // Lives at screen_base (typically 0x10080000, immediately above SheepMem).
    // QuickDraw issues ~1M stw/lwz ops per frame against this region; without
    // an explicit mapping they fault as UC_ERR_UNMAPPED and the unmapped-skip
    // handler spins, pegging CPU and never making progress.
    // Round up to page size so uc_mem_map accepts the length.
    {
        uint8_t *fb_host = video_ppc_get_framebuffer_host();
        uint32_t fb_size = video_ppc_get_framebuffer_size();
        uint32_t fb_mac = ppc::screen_base;
        if (fb_host && fb_size && fb_mac) {
            constexpr size_t PAGE = 0x1000;
            size_t mapped_size = (fb_size + PAGE - 1) & ~(PAGE - 1);
            if (!map_region(g_uc, fb_mac, mapped_size,
                            UC_PROT_READ | UC_PROT_WRITE,
                            (void *)fb_host, "Framebuffer"))
                goto fail;
        } else {
            fprintf(stderr, "[Unicorn-PPC] WARNING: framebuffer not yet allocated "
                            "(host=%p size=%u screen_base=%08x) — QuickDraw writes will fault\n",
                    fb_host, fb_size, fb_mac);
        }
    }

    // Permanently reserve the EXEC_RETURN trampoline slot NOW, before any
    // SheepVar lifetimes can intervene. Reserving later during execute_macos_code
    // would place us in the middle of the stack, where subsequent SheepVar
    // destructors raise `data` above us and later Reserves overwrite the slot.
    {
        g_macos_trampoline = kpx_sheep_mem_reserve(4);
        WriteMac32(g_macos_trampoline, POWERPC_EXEC_RETURN);
        fprintf(stderr, "[Unicorn-PPC] execute_macos_code trampoline @0x%08x = 0x%08x\n",
                g_macos_trampoline, POWERPC_EXEC_RETURN);
    }

    // DR Emulator / DR Cache — KPX `munmap`s these before nanokernel boot
    // (cpu_ppc_kpx.cpp:1204-1206) so probes hit SIGSEGV → skip instruction
    // → register stays untouched. Under Unicorn we leave them unmapped and
    // let the unmapped-memory hook auto-map zero pages on first touch,
    // matching KPX's "read returns 0, write is a no-op" shape. They're then
    // remapped to real RW RAM after the first IRQ (uppc_remap_dr_probes_once).

    // Grand Central I/O controller (0xf3000000..0xf3020000). The 68k serial /
    // SCC / MACE drivers poll registers here (0xf3012000 = SCCA, 0xf3016000 =
    // SCCB, 0xf3018000 = MACE ENET). KPX lets these raw stores/loads SIGSEGV
    // and silently skips the host instruction — the guest never blocks because
    // each skip drains one instruction per fault. Under Unicorn each fault
    // costs a full uc_emu_start teardown + unmapped-skip, and the tightloop
    // polling SCC-status-until-ready never progresses in wall time.
    // Stub it as MMIO: reads return 0 (driver sees "not busy / no data"),
    // writes are discarded. Matches SheepShaver's 68k-backend dummy I/O map.
    {
        static auto gc_read_cb = [](uc_engine *, uint64_t, unsigned, void *) -> uint64_t {
            return 0;
        };
        static auto gc_write_cb = [](uc_engine *, uint64_t, unsigned, uint64_t, void *) {
        };
        uc_err gc_err = uc_mmio_map(g_uc, 0xf3000000, 0x20000,
                                    (uc_cb_mmio_read_t)(uint64_t (*)(uc_engine *, uint64_t, unsigned, void *))gc_read_cb,
                                    nullptr,
                                    (uc_cb_mmio_write_t)(void (*)(uc_engine *, uint64_t, unsigned, uint64_t, void *))gc_write_cb,
                                    nullptr);
        if (gc_err != UC_ERR_OK) {
            fprintf(stderr, "[Unicorn-PPC] uc_mmio_map(GrandCentral) failed: %s\n",
                    uc_strerror(gc_err));
            goto fail;
        }
        fprintf(stderr, "[Unicorn-PPC] mapped %-12s mac=0x%08x size=0x%08x (MMIO stub)\n",
                "GrandCentral", 0xf3000000, 0x20000);
    }

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
        // Dump around 0x5046d728 — where r1 goes to 0 just before the InstallDrivers crash.
        fprintf(stderr, "[Unicorn-PPC] --- dump 0x5046d700..0x5046d760 ---\n");
        for (uint32_t a = 0x5046d700; a < 0x5046d760; a += 16) {
            uint32_t buf[4] = {0,0,0,0};
            if (uc_mem_read(g_uc, a, buf, sizeof(buf)) == UC_ERR_OK) {
                fprintf(stderr,
                    "[Unicorn-PPC] ROM @0x%08x: %08x %08x %08x %08x\n",
                    a, __builtin_bswap32(buf[0]), __builtin_bswap32(buf[1]),
                    __builtin_bswap32(buf[2]), __builtin_bswap32(buf[3]));
            }
        }
        // Dump around 0x5046f900 — new r1-corruption site after SCALE=10 unlocks
        // the DiskPrime #800+ path (boot blocks fully loaded, CPU back in ROM).
        // Block transitions land at 0x5046f900 → 0x5046f90c with r1 going from a
        // valid RAM address (~0x05ff8044) to a tiny value like 0x00000002.
        fprintf(stderr, "[Unicorn-PPC] --- dump 0x5046f8e0..0x5046f940 ---\n");
        for (uint32_t a = 0x5046f8e0; a < 0x5046f940; a += 16) {
            uint32_t buf[4] = {0,0,0,0};
            if (uc_mem_read(g_uc, a, buf, sizeof(buf)) == UC_ERR_OK) {
                fprintf(stderr,
                    "[Unicorn-PPC] ROM @0x%08x: %08x %08x %08x %08x\n",
                    a, __builtin_bswap32(buf[0]), __builtin_bswap32(buf[1]),
                    __builtin_bswap32(buf[2]), __builtin_bswap32(buf[3]));
            }
        }
        // Dump around 0x504a73a8 — the probe-table where the repeat-skip loop
        // stalls (r1 below KD, `lwz r24, 0(r1)` keeps faulting then branches
        // to 0x50467da0 / 0x50467de0, which returns back to the table).
        fprintf(stderr, "[Unicorn-PPC] --- dump 0x504a7380..0x504a73e0 ---\n");
        for (uint32_t a = 0x504a7380; a < 0x504a73e0; a += 16) {
            uint32_t buf[4] = {0,0,0,0};
            if (uc_mem_read(g_uc, a, buf, sizeof(buf)) == UC_ERR_OK) {
                fprintf(stderr,
                    "[Unicorn-PPC] ROM @0x%08x: %08x %08x %08x %08x\n",
                    a, __builtin_bswap32(buf[0]), __builtin_bswap32(buf[1]),
                    __builtin_bswap32(buf[2]), __builtin_bswap32(buf[3]));
            }
        }
        // Dump 0x50467d80..0x50467e20 — the handler that the probe-table
        // branches to. Tail-branches back into 0x504a73xx and forms the loop.
        fprintf(stderr, "[Unicorn-PPC] --- dump 0x50467d80..0x50467e20 ---\n");
        for (uint32_t a = 0x50467d80; a < 0x50467e20; a += 16) {
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

    // SheepMem address holding EXEC_RETURN as a PPC instruction. Used as a
    // trampoline so blr / mtctr+bctr through LR/r10/r12 lands on a mapped
    // address (which dispatches via gen_mac_emulop) instead of 0x18000000.
    uint32_t trampoline = g_macos_trampoline;

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
    wr_gpr(6, ksave);
    wr_gpr(7, ReadMac32(KERNEL_DATA_BASE + 0x660));
    wr_gpr(8, 0);
    wr_lr(trampoline);
    wr_gpr(10, trampoline);
    wr_gpr(12, trampoline);
    wr_gpr(13, rd_cr());

    {
        uint32_t x7 = rd_gpr(7);
        uint32_t rot = (x7 << 8) | (x7 >> 24);
        uint32_t r7_new = (rot & 0x80000000u) | (x7 & ~0x80000000u);
        wr_gpr(7, r7_new);

        uint32_t xer = 0;
        uc_reg_read(g_uc, UC_PPC_REG_XER, &xer);
        uint32_t cr_now = rd_cr();
        uint32_t cr0 = 0;
        int32_t sv = (int32_t)r7_new;
        if (sv < 0)      cr0 |= 0x8;
        else if (sv > 0) cr0 |= 0x4;
        else             cr0 |= 0x2;
        if (xer & 0x80000000u) cr0 |= 0x1;
        wr_cr((cr_now & 0x0fffffffu) | (cr0 << 28));
    }

    wr_gpr(11, 0xf072);
    {
        uint32_t cr = rd_cr();
        wr_cr((rd_gpr(11) & 0x0fff0000u) | (cr & ~0x0fff0000u));
    }

    if (ppc_trace_stream_()) {
        fprintf(ppc_trace_stream_(),
                "%08llu IRQ    entry=%08x saved_pc=%08x saved_cr=%08x\n",
                (unsigned long long)ppc_trace_seq_(), entry, saved_pc, rd_gpr(13));
    }

    // Run nanokernel IRQ handler; returns when EXEC_RETURN fires.
    // Bump nesting depth so the tick thread and EmulOp-boundary stop guards
    // know not to interrupt us mid-handler.
    wr_pc(entry);
    ++g_emu_nest_depth;
    uc_err err = uc_emu_start(g_uc, entry, 0, 0, 0);
    --g_emu_nest_depth;
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
    static const bool s_trace_irq = [](){
        const char* e = std::getenv("MACEMU_PPC_TRACE_IRQ");
        return e && *e && *e != '0';
    }();

    int32_t nest = (int32_t)ReadMac32(XLM_IRQ_NEST);
    if (nest > 0) {
        if (s_trace_irq) {
            fprintf(stderr, "[IRQ] skip (nest=%d) pc=%08x\n", nest, rd_pc());
        }
        return;
    }

    uint32_t mode = ReadMac32(XLM_RUN_MODE);

    switch (mode) {
    case MODE_68K: {
        uint32_t or_mask = ReadMac32(KERNEL_DATA_BASE + 0x674);
        uint32_t cr_before = rd_cr();
        WriteMac16(ReadMac32(KERNEL_DATA_BASE + 0x67c), 1);
        wr_cr(cr_before | or_mask);
        if (s_trace_irq) {
            uint32_t ticks = ReadMac32(0x016a);
            uint32_t d0 = rd_gpr(8);
            fprintf(stderr,
                    "[IRQ] MODE_68K pc=%08x cr_before=%08x cr_after=%08x r22=%08x r24=%08x Ticks=%u D0=%08x\n",
                    rd_pc(), cr_before, rd_cr(), rd_gpr(22), rd_gpr(24), ticks, d0);
        }
        if (ppc_trace_stream_()) {
            fprintf(ppc_trace_stream_(),
                    "%08llu MODE68K cr_before=%08x or_mask=%08x cr_after=%08x\n",
                    (unsigned long long)ppc_trace_seq_(), cr_before, or_mask, rd_cr());
        }
        break;
    }

    case MODE_NATIVE:
        if (rd_gpr(1) != ppc::KernelDataAddr) {
            WriteMac16(ReadMac32(KERNEL_DATA_BASE + 0x67c), 1);
            uint32_t kframe = ReadMac32(KERNEL_DATA_BASE + 0x658) + 0xdc;
            WriteMac32(kframe, ReadMac32(kframe) | ReadMac32(KERNEL_DATA_BASE + 0x674));

            // Mirror KPX HandleInterrupt: increment XLM_IRQ_NEST (DisableInterrupt
            // semantics), run the NK handler, and let the ROM patch at 0x318000
            // decrement nest on the handler's return path. Do NOT reset nest to 0
            // unconditionally — that clobbers accumulated nest levels from ROM
            // patches (e.g. MixedMode entry at 0x36fa00) and causes HandleInterrupt
            // to re-fire during partial-handler execution.
            int32_t new_nest = (int32_t)ReadMac32(XLM_IRQ_NEST) + 1;
            WriteMac32(XLM_IRQ_NEST, new_nest);
            uint32_t entry = (ppc::ROMType == ROMTYPE_NEWWORLD)
                ? ppc::ROMBase + 0x312b1c
                : ppc::ROMBase + 0x312a3c;
            uppc_interrupt(entry);
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

    // MACEMU_PPC_TICK_PERIOD_SCALE scales the 16.625ms 60Hz tick.
    // Default 1 → true 60Hz. Since the block-hook IRQ delivery landed
    // (commit 1e9dfbd0), SCALE=1 reaches Finder in ~10s; before it was
    // the best setting only for debugging and boot would stall at
    // "Loading boot blocks". Raise to 10 (~6Hz) only if investigating
    // the old IRQ-pressure path.
    static const int s_tick_scale = [](){
        const char* e = std::getenv("MACEMU_PPC_TICK_PERIOD_SCALE");
        int v = (e && *e) ? atoi(e) : 1;
        return v > 0 ? v : 1;
    }();
    while (g_tick_thread_running.load()) {
        const int period_us = 16625 * s_tick_scale;
        next += period_us;
        int64_t delay = (int64_t)(next - GetTicks_usec());
        if (delay > 0) {
            struct timespec ts = {0, (long)(delay * 1000)};
            nanosleep(&ts, nullptr);
        } else if (delay < -period_us) {
            next = GetTicks_usec();
        }
        if (tick_inhibit) continue;

        // MACEMU_PPC_NO_IRQ suppresses all async IRQ injection so the
        // KPX-vs-Unicorn EmulOp boundary trace is deterministic (see
        // docs/ppc/UnicornPpcPlan.md debug §). Boot won't progress past the
        // first async-IRQ-dependent point, but the pre-IRQ window is a clean
        // comparison baseline.
        static const bool s_no_irq = [](){
            const char* e = std::getenv("MACEMU_PPC_NO_IRQ");
            return e && *e && *e != '0';
        }();
        if (s_no_irq) continue;

        if (++tick_counter > 60) {
            tick_counter = 0;
            WriteMac32(0x20c, (uint32_t)time(nullptr) + 0x7C25B080u);
        }

        uint32_t nest = ReadMac32(XLM_IRQ_NEST);
        // MACEMU_PPC_MIN_EMULOPS_PER_IRQ: minimum emulops between IRQs.
        // KPX boots with ~42 emulops between IRQs on average; Unicorn is ~10x
        // slower, so 60Hz wall-clock ticks land roughly every 1.4 emulops in
        // Unicorn. This starves the 68k side: most EmulOp boundaries see
        // CR2.{LT,GT,EQ} polluted by the IRQ's OR-of-0xe00000 before the
        // code that expects a clean CR2 runs. Gating on emulop count keeps
        // the IRQ cadence at a KPX-like ratio so critical sections run
        // to completion before the next IRQ fires.
        static const uint64_t s_min_emulops = [](){
            const char* e = std::getenv("MACEMU_PPC_MIN_EMULOPS_PER_IRQ");
            return (e && *e && *e != '0') ? std::strtoull(e, nullptr, 10) : 0ull;
        }();
        static uint64_t s_last_irq_emulop = 0;
        if (s_min_emulops && g_emulop_count - s_last_irq_emulop < s_min_emulops) continue;

        // MACEMU_PPC_DEFER_FIRST_IRQ=N: suppress 60Hz tick IRQs until
        // g_emulop_count reaches N. KPX's natural first-IRQ cadence is ~105
        // emulops from reset; Unicorn's is ~9. The premature first tick lands
        // inside early ROM init where CR2.{LT,GT,EQ} are load-bearing and gets
        // OR'd with 0xe00000, causing divergent boot paths.
        static const uint64_t s_defer_first = [](){
            const char* e = std::getenv("MACEMU_PPC_DEFER_FIRST_IRQ");
            return (e && *e && *e != '0') ? std::strtoull(e, nullptr, 10) : 0ull;
        }();
        if (s_defer_first && g_emulop_count < s_defer_first) continue;

        if (nest == 0) {
            SetInterruptFlag(INTFLAG_VIA);
            // Set the pending-IRQ flag only — do NOT call uc_emu_stop from
            // this thread. The always-on UC_HOOK_BLOCK (see cpu_init) picks
            // up the flag at the next TB boundary and exits cleanly. That
            // avoids the cross-thread async stop which tore TBs mid-execution
            // and collapsed SCALE=1 throughput ~50×.
            //
            // Fallback: opting out with MACEMU_PPC_NO_IRQ_HOOK=1 re-enables
            // the legacy rising-edge kick so this thread can still punch
            // idle loops that never hit a block boundary (can't happen in
            // practice — any branch is a TB boundary — but kept for rollback).
            bool was_pending = g_pending_irq.exchange(true);
            s_last_irq_emulop = g_emulop_count;
            static const bool s_no_irq_hook = [](){
                const char* e = std::getenv("MACEMU_PPC_NO_IRQ_HOOK");
                return e && *e && *e != '0';
            }();
            if (s_no_irq_hook && !was_pending && g_uc && g_emu_nest_depth <= 1)
                uc_emu_stop(g_uc);
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
    // Hot-skip-loop detection: if the same PC is skipped N consecutive times
    // (no other PC interleaved, no EmulOp advance), we're spinning on a probe
    // the guest can't exit. Bail with diagnostics rather than hang forever.
    uint32_t last_skip_pc = 0;
    uint64_t consecutive_same_pc = 0;
    constexpr uint64_t kHotSkipBailThreshold = 100000;
    // Progress watchdog: bail out if g_emulop_count doesn't advance for
    // kProgressStallBailMs wall-ms. Covers both valid-memory tight loops
    // (uc_emu_start returns UC_ERR_OK on 1s timeout) and unmapped-walk
    // loops (each UC_ERR_*_UNMAPPED iteration is microseconds). Measure
    // wall time, not iteration count — iteration cadence varies by 6+
    // orders of magnitude between these two failure modes.
    uint64_t last_progress_emulop = g_emulop_count;
    uint32_t last_progress_r24    = rd_gpr(24);
    auto last_progress_ts = std::chrono::steady_clock::now();
    uint64_t iters_since_progress = 0;
    uint64_t irqs_since_progress = 0;
    constexpr int kProgressStallBailMs = 5000;
    // r24 (68k PC) snapshot ring. Populated once per main-loop iteration so
    // the progress-stall bail can show whether the 68k emulator was making
    // forward progress (r24 advancing) or literally stuck on one 68k insn
    // during the stall window. Size 64 → covers ~1s of stall at 60Hz ticks.
    // Also drives the r24-advance watchdog: if the 68k PC ever changes, the
    // guest is making forward 68k progress (even if it doesn't dispatch an
    // EmulOp — a tight 68k ALU loop in ROM init is a legitimate example).
    uint32_t iter_r24_ring[64] = {0};
    int      iter_r24_idx = 0;
    // Concentrated-skip bail: catches 2–3 PC alternating unmapped-write loops
    // where consecutive_same_pc resets each toggle. If we've silently skipped
    // many stores confined to very few PCs, something is wedged (usually a
    // stack-push loop with a corrupt r1/r21) — bail so the state surfaces.
    constexpr uint64_t kConcentratedSkipTotal = 200;
    constexpr size_t kConcentratedSkipMaxPcs = 6;
    // Block-PC window watchdog: catches tight loops that DO fire EmulOps
    // (so progress-stall never trips) but stay inside a narrow code window.
    // Checks the always-on 32-entry g_uppc_last_block_pcs ring: if every
    // recent block-entry PC falls within a kBlockWindowBytes window for
    // kBlockWindowStallMs wall-ms, bail. Boot-block loading legitimately
    // cycles tightly, but not for many seconds — KPX crosses the milestone
    // in <1 s.
    constexpr uint32_t kBlockWindowBytes = 0x1000;  // 4 KB
    constexpr int kBlockWindowStallMs = 8000;
    auto last_block_window_break_ts = std::chrono::steady_clock::now();
    // Yield periodically so IRQs get polled even when the guest sits in a
    // pure-PPC code stretch without EmulOp dispatches. Without this, a tight
    // PPC loop (e.g. the NK idle/wait path) starves pending_irq and the 68k
    // emulator never gets a VBL. Unicorn's timeout is in microseconds, but
    // short timeouts (<100ms) yield far too aggressively and torpedo throughput
    // — empirically 1 insn per call at 50ms. Use a loose 1s ceiling: only a
    // true PPC-only tight loop will hit it, and at 1Hz-yield the IRQ delivery
    // is still well within the OS's tolerance.
    const uint64_t s_emu_timeout_us = 1000000;
    // SMC-staleness experiment: MACEMU_PPC_TB_FLUSH_EVERY flushes the TB cache
    // every outer-loop iteration, forcing TCG to re-translate all code. Tests
    // whether the non-deterministic boot stall is caused by stale translations
    // over self-modified code pages (stubbed cpu_physical_memory_set_dirty_flag).
    const bool s_tb_flush_every = (getenv("MACEMU_PPC_TB_FLUSH_EVERY") != nullptr);
    if (s_tb_flush_every) {
        fprintf(stderr, "[Unicorn-PPC] MACEMU_PPC_TB_FLUSH_EVERY=1 — flushing TB every iter\n");
    }
    while (!g_stop_requested) {
        if (s_tb_flush_every) {
            uc_ctl_flush_tb(g_uc);
        }
        uint32_t pc = rd_pc();
        ++g_emu_nest_depth;
        uint64_t emulops_before = g_emulop_count;
        uc_err err = uc_emu_start(g_uc, pc, 0, s_emu_timeout_us, /*count*/0);
        --g_emu_nest_depth;
        iter_r24_ring[iter_r24_idx & 63] = rd_gpr(24);
        iter_r24_idx++;
        if (err == UC_ERR_READ_UNMAPPED || err == UC_ERR_WRITE_UNMAPPED) {
            // KPX SIGSEGV-skip shape: step past the dead instruction and keep
            // going. Log the first few per-PC so probe loops don't flood the
            // terminal but genuine bugs still surface.
            uint32_t after = rd_pc();
            static std::unordered_map<uint32_t, uint64_t> skip_counts;
            uint64_t &c = skip_counts[after];
            if (c < 3) {
                uint32_t insn = 0;
                uc_mem_read(g_uc, after, &insn, 4);
                insn = __builtin_bswap32(insn);
                uint32_t ra_idx = (insn >> 16) & 0x1f;
                int16_t imm = (int16_t)(insn & 0xffff);
                uint32_t tgt = (ra_idx == 0 ? 0 : rd_gpr(ra_idx)) + (uint32_t)(int32_t)imm;
                fprintf(stderr, "[Unicorn-PPC] skip %s @pc=0x%08x insn=%08x r%u=%08x tgt=%08x lr=%08x (n=%llu)\n",
                        (err == UC_ERR_READ_UNMAPPED) ? "READ" : "WRITE",
                        after, insn, ra_idx, rd_gpr(ra_idx), tgt, rd_lr(), (unsigned long long)(c + 1));
            }
            c++; skipped++;

            // Hot-skip-loop bail-out. If the same PC faults kHotSkipBailThreshold
            // times in a row (no other PC interleaved, no EmulOp advance), we're
            // spinning and will never escape. Dump a full snapshot so next
            // session can figure out why the 68k-level loop counter never
            // reaches its exit condition.
            if (after == last_skip_pc) {
                if (++consecutive_same_pc >= kHotSkipBailThreshold) {
                    uint32_t insn = 0;
                    uc_mem_read(g_uc, after, &insn, 4);
                    insn = __builtin_bswap32(insn);
                    fprintf(stderr,
                            "[Unicorn-PPC] hot-skip loop at pc=0x%08x insn=%08x "
                            "(%llu consecutive skips) — bailing\n"
                            "  lr=%08x ctr=%08x cr=%08x xer=%08x\n",
                            after, insn,
                            (unsigned long long)consecutive_same_pc,
                            rd_lr(), rd_ctr(), rd_cr(), rd_xer());
                    for (int i = 0; i < 32; ++i) {
                        fprintf(stderr, "  r%-2d=%08x%s", i, rd_gpr(i),
                                (i % 4 == 3) ? "\n" : "");
                    }
                    // Dump 16 instructions around the stuck PC so next session
                    // can see the full loop body (not just the faulting op).
                    fprintf(stderr, "  --- instructions @ pc-0x20 .. pc+0x20 ---\n");
                    for (int off = -0x20; off <= 0x20; off += 4) {
                        uint32_t ins = 0;
                        uint32_t at = after + off;
                        if (uc_mem_read(g_uc, at, &ins, 4) == UC_ERR_OK) {
                            ins = __builtin_bswap32(ins);
                            fprintf(stderr, "    %08x: %08x%s\n", at, ins,
                                    off == 0 ? "  <== stuck" : "");
                        }
                    }
                    break;
                }
            } else {
                last_skip_pc = after;
                consecutive_same_pc = 1;
            }

            // Concentrated-skip bail. Catches 2–3 PC alternating loops that
            // consecutive_same_pc can't see (each toggle resets it).
            if (skipped >= kConcentratedSkipTotal &&
                skip_counts.size() <= kConcentratedSkipMaxPcs) {
                fprintf(stderr,
                        "[Unicorn-PPC] concentrated-skip bail: %llu skips "
                        "across %zu distinct PCs — tight unmapped-write loop\n",
                        (unsigned long long)skipped, skip_counts.size());
                for (auto &kv : skip_counts) {
                    uint32_t ins = 0;
                    uc_mem_read(g_uc, kv.first, &ins, 4);
                    ins = __builtin_bswap32(ins);
                    fprintf(stderr, "  pc=0x%08x insn=%08x hits=%llu\n",
                            kv.first, ins, (unsigned long long)kv.second);
                }
                fprintf(stderr,
                        "  r1=%08x r21=%08x lr=%08x ctr=%08x cr=%08x\n",
                        rd_gpr(1), rd_gpr(21), rd_lr(), rd_ctr(), rd_cr());
                for (int i = 0; i < 32; ++i) {
                    fprintf(stderr, "  r%-2d=%08x%s", i, rd_gpr(i),
                            (i % 4 == 3) ? "\n" : "");
                }
                break;
            }

            if (!uppc_skip_memop_at(after)) {
                fprintf(stderr, "[Unicorn-PPC] unknown memop at pc=0x%08x — bailing\n", after);
                break;
            }
            stuck_count = 0;
            // Fall through to progress watchdog below — don't `continue` past
            // it, otherwise unmapped-walk loops without EmulOp advance are
            // never bailed.
        } else if (err != UC_ERR_OK) {
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

        // Progress watchdog — covers every outer-loop iteration (any err
        // status). Bails when EmulOp dispatches haven't advanced for
        // kProgressStallBailMs wall-ms — catches both valid-memory tight
        // loops and unmapped-walk loops.
        ++iters_since_progress;
        uint32_t cur_r24 = rd_gpr(24);
        // Either an EmulOp dispatch or a 68k-PC (r24) advance counts as
        // forward progress. A tight 68k ALU loop in ROM init (MOVE/BNE/ADDA)
        // produces no EmulOps but does move r24 through its body — at
        // Unicorn TCG speed that can easily exceed the 5-s emulop watchdog.
        if (g_emulop_count != last_progress_emulop ||
            cur_r24 != last_progress_r24) {
            last_progress_emulop = g_emulop_count;
            last_progress_r24    = cur_r24;
            last_progress_ts = std::chrono::steady_clock::now();
            iters_since_progress = 0;
            irqs_since_progress = 0;
        } else {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_progress_ts).count();
            if (elapsed_ms >= kProgressStallBailMs) {
                uint32_t stall_pc = rd_pc();
                uint32_t insn = 0;
                uc_mem_read(g_uc, stall_pc, &insn, 4);
                insn = __builtin_bswap32(insn);
                uint32_t nest_val = ReadMac32(XLM_IRQ_NEST);
                uint32_t run_mode = ReadMac32(XLM_RUN_MODE);
                fprintf(stderr,
                        "[Unicorn-PPC] progress stall: no EmulOp advance for "
                        "%lldms (%llu iters, %llu IRQs delivered, %llu skips) "
                        "XLM_IRQ_NEST=%d XLM_RUN_MODE=%u — bailing\n"
                        "  pc=0x%08x insn=%08x lr=%08x ctr=%08x cr=%08x xer=%08x\n",
                        (long long)elapsed_ms,
                        (unsigned long long)iters_since_progress,
                        (unsigned long long)irqs_since_progress,
                        (unsigned long long)skipped,
                        (int)nest_val, run_mode,
                        stall_pc, insn, rd_lr(), rd_ctr(), rd_cr(), rd_xer());
                for (int i = 0; i < 32; ++i) {
                    fprintf(stderr, "  r%-2d=%08x%s", i, rd_gpr(i),
                            (i % 4 == 3) ? "\n" : "");
                }
                fprintf(stderr, "  --- instructions @ pc-0x20..pc+0x20 ---\n");
                for (int off = -0x20; off <= 0x20; off += 4) {
                    uint32_t ins = 0;
                    uint32_t at = stall_pc + off;
                    if (uc_mem_read(g_uc, at, &ins, 4) == UC_ERR_OK) {
                        ins = __builtin_bswap32(ins);
                        fprintf(stderr, "    %08x: %08x%s\n", at, ins,
                                off == 0 ? "  <== stall" : "");
                    }
                }
                fprintf(stderr, "  --- instructions @ lr-0x20..lr+0x10 ---\n");
                for (int off = -0x20; off <= 0x10; off += 4) {
                    uint32_t ins = 0;
                    uint32_t at = rd_lr() + off;
                    if (uc_mem_read(g_uc, at, &ins, 4) == UC_ERR_OK) {
                        ins = __builtin_bswap32(ins);
                        fprintf(stderr, "    %08x: %08x%s\n", at, ins,
                                off == 0 ? "  <== lr" : "");
                    }
                }
                fprintf(stderr, "  recent block PCs (ring; newest at idx=%d):\n   ",
                        g_uppc_last_block_pcs_idx);
                for (int i = 0; i < 32; ++i) {
                    fprintf(stderr, " %08x", g_uppc_last_block_pcs[i]);
                    if (i == 15) fprintf(stderr, "\n   ");
                }
                fprintf(stderr, "\n");
                // 68k opcode bytes at current r24 — four halfwords big-endian.
                // Lets us see what 68k instruction the emulator was stuck on.
                uint32_t r24_now = rd_gpr(24);
                fprintf(stderr, "  68k code @ r24=0x%08x:", r24_now);
                for (int off = 0; off < 16; off += 2) {
                    uint16_t hw = 0;
                    if (uc_mem_read(g_uc, r24_now + off, &hw, 2) == UC_ERR_OK) {
                        fprintf(stderr, " %04x", __builtin_bswap16(hw));
                    } else {
                        fprintf(stderr, " ????");
                    }
                }
                fprintf(stderr, "\n");
                // r24 trajectory: last 64 main-loop iterations. If all values
                // are identical → 68k PC stuck (handler hang). If varying →
                // 68k emulator making progress but too slow to dispatch an
                // EmulOp within the stall window (Unicorn TCG throughput).
                fprintf(stderr, "  r24 (68k PC) ring (last %d iters, oldest->newest):\n   ",
                        (iter_r24_idx < 64 ? iter_r24_idx : 64));
                int r24_show = iter_r24_idx < 64 ? iter_r24_idx : 64;
                for (int k = iter_r24_idx - r24_show; k < iter_r24_idx; ++k) {
                    fprintf(stderr, " %08x", iter_r24_ring[k & 63]);
                    if (((k - (iter_r24_idx - r24_show)) & 7) == 7) fprintf(stderr, "\n   ");
                }
                fprintf(stderr, "\n");
                break;
            }
        }

        // Block-PC window watchdog. EmulOps can keep advancing (e.g. CHECKLOAD
        // spinning in boot-block-load) so progress-stall never trips — but if
        // every recent block-entry PC sits inside a 4 KB window for many
        // seconds, we're in a localized loop that won't exit. Snapshot the
        // always-on ring non-atomically; a torn read at worst delays the bail
        // by one iteration.
        {
            uint32_t lo = UINT32_MAX, hi = 0;
            int non_zero = 0;
            for (int i = 0; i < 32; ++i) {
                uint32_t p = g_uppc_last_block_pcs[i];
                if (!p) continue;
                non_zero++;
                if (p < lo) lo = p;
                if (p > hi) hi = p;
            }
            bool window_tight = (non_zero >= 16) && (hi - lo < kBlockWindowBytes);
            if (!window_tight) {
                last_block_window_break_ts = std::chrono::steady_clock::now();
            } else {
                auto tight_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - last_block_window_break_ts).count();
                if (tight_ms >= kBlockWindowStallMs) {
                    fprintf(stderr,
                            "[Unicorn-PPC] block-window stall: last 32 block "
                            "PCs span [%08x..%08x] (%u bytes) for %lldms — "
                            "bailing (emulops=%llu pc=%08x lr=%08x)\n",
                            lo, hi, hi - lo, (long long)tight_ms,
                            (unsigned long long)g_emulop_count, rd_pc(), rd_lr());
                    fprintf(stderr, "  recent block PCs (ring, unordered):\n   ");
                    for (int i = 0; i < 32; ++i) {
                        fprintf(stderr, " %08x", g_uppc_last_block_pcs[i]);
                        if (i == 15) fprintf(stderr, "\n   ");
                    }
                    fprintf(stderr, "\n");
                    for (int i = 0; i < 32; ++i) {
                        fprintf(stderr, "  r%-2d=%08x%s", i, rd_gpr(i),
                                (i % 4 == 3) ? "\n" : "");
                    }
                    fprintf(stderr, "  ctr=%08x cr=%08x xer=%08x\n",
                            rd_ctr(), rd_cr(), rd_xer());
                    break;
                }
            }
        }

        if (g_pending_irq.exchange(false)) {
            uppc_handle_interrupt();
            ++irqs_since_progress;
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
    // Match KPX's cooperative-at-block-boundary semantics: flag only, no
    // mid-block abort. The main loop polls g_pending_irq at every
    // uc_emu_start return (every EmulOp). See comment in uppc_tick_thread.
    g_pending_irq.store(true);
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
    // LR must point at a SheepMem slot containing the EXEC_RETURN instruction,
    // not the literal sentinel value. Guest functions save/restore LR through
    // their stack frames; if LR holds 0x18000001, a later blr fetches PC =
    // 0x18000000 (low 2 bits masked) which is unmapped. The trampoline address
    // survives prologue/epilogue round-trips and dispatches via gen_mac_emulop.
    wr_lr(g_macos_trampoline);
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
        ++g_emu_nest_depth;
        uc_err err = uc_emu_start(g_uc, run_pc, 0, 0, 0);
        --g_emu_nest_depth;
        if (err == UC_ERR_OK) {
            // Distinguish EXEC_RETURN (caller-visible return) from a spurious
            // uc_emu_stop (tick thread IRQ kick). A spurious stop would leave
            // g_exec_return_seen false; restart at the current PC so the
            // caller's 68k/PPC code finishes its work. Failing to restart
            // corrupts the caller's D0/A-regs (e.g. FindSymbol returns 0).
            if (g_exec_return_seen) {
                g_exec_return_seen = false;
                break;
            }
            run_pc = rd_pc();
            continue;
        }
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
        uint32_t lr_now = rd_lr();
        uint32_t r1_now = rd_gpr(1);
        fprintf(stderr, "[Unicorn-PPC] execute_ppc(0x%08x) err=%s pc=0x%08x lr=0x%08x r1=0x%08x\n",
                entry, uc_strerror(err), after, lr_now, r1_now);
        if (after == stuck_pc && ++stuck_count >= 3) break;
        if (after != stuck_pc) { stuck_pc = after; stuck_count = 1; }
        run_pc = after;
        if (g_stop_requested) break;
    }

    static const bool s_trace_trap_exit = []() {
        const char* e = std::getenv("MACEMU_PPC_TRACE_TRAP");
        return e && *e && *e != '0';
    }();
    if (s_trace_trap_exit) {
        uint32_t exit_pc = rd_pc();
        uint32_t opc = 0;
        uc_mem_read(g_uc, exit_pc, &opc, 4);
        // Big-endian opcode
        opc = __builtin_bswap32(opc);
        fprintf(stderr, "[Unicorn-PPC]     execute_ppc exit pc=0x%08x opcode=0x%08x (restoring saved_pc=0x%08x) lr=0x%08x r1=0x%08x r24=0x%08x\n",
                exit_pc, opc, saved_pc, rd_lr(), rd_gpr(1), rd_gpr(24));
    }

    wr_pc(saved_pc);
    wr_lr(saved_lr);
}

// ----- execute_macos_code — port of sheepshaver_cpu::execute_macos_code ----
// Invokes a PPC shared-library function via its transition vector (tvect)
// from host context. Used by call_macos*() in cpu_ppc_kpx.cpp when the
// Unicorn backend is active (ppc_cpu == nullptr).
//
// Matches KPX exactly (cpu_ppc_kpx.cpp:796-829): reads proc/toc from the
// tvect, sets r3..r(3+nargs-1) to the args with r2 = toc, LR to a SheepMem
// trampoline containing POWERPC_EXEC_RETURN, then runs until that sentinel
// fires. Returns gpr(3).

// Externally visible so call_macos*() in cpu_ppc_kpx.cpp can dispatch here
// when ppc_cpu (the KPX singleton) is nullptr (i.e. Unicorn backend active).
extern "C" uint32_t uppc_cpu_execute_macos_code(uint32_t tvect, int nargs, uint32_t const *args);
extern "C" uint32_t uppc_cpu_execute_macos_code(uint32_t tvect, int nargs, uint32_t const *args)
{
    if (!g_uc || !g_macos_trampoline) return 0;

    uint32_t saved_pc  = rd_pc();
    uint32_t saved_lr  = rd_lr();
    uint32_t saved_ctr = rd_ctr();

    wr_lr(g_macos_trampoline);

    // KPX reserves 64 bytes of stack for the MacOS call frame.
    uint32_t saved_sp = rd_gpr(1);
    wr_gpr(1, saved_sp - 64);

    uint32_t proc = ReadMac32(tvect);
    uint32_t toc  = ReadMac32(tvect + 4);

    // Save r2..r(2+nargs) so we can restore them after the call.
    uint32_t saved_gprs[9] = {0};
    saved_gprs[0] = rd_gpr(2);
    for (int i = 0; i < nargs && i < 8; i++)
        saved_gprs[i + 1] = rd_gpr(i + 3);

    wr_gpr(2, toc);
    for (int i = 0; i < nargs && i < 8; i++)
        wr_gpr(i + 3, args[i]);

    uppc_cpu_execute_ppc(proc);
    uint32_t retval = rd_gpr(3);

    // Restore r2..r(2+nargs).
    for (int i = 0; i <= nargs && i <= 8; i++)
        wr_gpr(i + 2, saved_gprs[i]);

    wr_gpr(1, saved_sp);
    wr_pc(saved_pc);
    wr_lr(saved_lr);
    wr_ctr(saved_ctr);

    return retval;
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

    // Supervisor bit in CR2.SO (bit 11) — matches KPX's CR_SO_field<2>::mask()
    // (sheepshaver_cpu::execute_68k). The 68k-emulator opcode dispatch uses
    // CR2.SO as its privilege flag; setting CR0.SO instead makes later
    // conditional branches in the dispatch (e.g. 0x5046d720) take the wrong
    // side and leave r1=0 at 0x5046d728's bclrl.
    wr_cr(0x00100000u);

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

    static const bool s_trace_trap = []() {
        const char* e = std::getenv("MACEMU_PPC_TRACE_TRAP");
        return e && *e && *e != '0';
    }();
    if (s_trace_trap) {
        fprintf(stderr, "[Unicorn-PPC]   execute_68k entry=0x%08x op=0x%04x dispatch=0x%08x\n",
                entry, opcode, rd_gpr(29));
    }

    uppc_cpu_execute_ppc(rd_gpr(29));

    if (s_trace_trap) {
        fprintf(stderr, "[Unicorn-PPC]   execute_68k done  pc=0x%08x lr=0x%08x r24=0x%08x r1=0x%08x\n",
                rd_pc(), rd_lr(), rd_gpr(24), rd_gpr(1));
    }

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
    static const bool s_trace_trap = []() {
        const char* e = std::getenv("MACEMU_PPC_TRACE_TRAP");
        return e && *e && *e != '0';
    }();
    if (s_trace_trap) {
        fprintf(stderr, "[Unicorn-PPC] execute_68k_trap enter trap=0x%04x d0=%08x a0=%08x sp=%08x\n",
                trap, r->d[0], r->a[0], rd_gpr(1));
    }
    static bool dumped = false;
    if (!dumped) {
        dumped = true;
        fprintf(stderr, "[Unicorn-PPC] First trap 0x%04x — KD+0x1000..0x1080 dump:\n", trap);
        for (uint32_t a = 0x68fff000; a < 0x68fff080; a += 16) {
            uint32_t buf[4] = {0,0,0,0};
            uc_mem_read(g_uc, a, buf, sizeof(buf));
            fprintf(stderr, "  0x%08x: %08x %08x %08x %08x\n",
                    a, __builtin_bswap32(buf[0]), __builtin_bswap32(buf[1]),
                    __builtin_bswap32(buf[2]), __builtin_bswap32(buf[3]));
        }
        fprintf(stderr, "[Unicorn-PPC]   r1=0x%08x (caller's) opcode_table=KD+0x1074=0x%08x emul=KD+0x1078=0x%08x\n",
                rd_gpr(1), ReadMac32(0x68fff074), ReadMac32(0x68fff078));
    }
    uppc_cpu_execute_68k(TRAP_PROC, r);
    if (s_trace_trap) {
        fprintf(stderr, "[Unicorn-PPC] execute_68k_trap exit  trap=0x%04x d0=%08x a0=%08x\n",
                trap, r->d[0], r->a[0]);
    }
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

// EXEC_NATIVE dispatch — reuse KPX's backend-agnostic pure dispatcher, which
// operates on the GPR array we marshal in uppc_dispatch_native_op. The handler
// body (NQD_*, ether_*, Serial*, VideoDoDriverIO, MakeExecutable,
// check_load_invoc, etc.) lives in libkpx_interp.a, linked unconditionally.
extern "C" void kpx_ppc_native_op(uint32_t selector, uint32_t gprs[32]);
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
    p->ppc_native_op = kpx_ppc_native_op;
    p->trap_handler = nullptr;
    p->ppc_cursor_move = uppc_ppc_cursor_move;
}

} // extern "C"

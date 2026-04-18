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
namespace ppc {
    extern void VideoInstallAccel(void);
    extern void VideoVBL(void);
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

// XLM fields from kpx/compat/xlowmem.h (REAL_ADDRESSING, Mac addr = host addr).
#define XLM_RUN_MODE          0x68ffec00u
#define XLM_IRQ_NEST          0x68ffec20u
#define XLM_68K_R25           0x68ffec0cu
#define XLM_EXEC_RETURN_OPCODE 0x68ffec1cu

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

    // Unmapped-memory hook. The nanokernel probes the 32-bit address space at
    // various offsets (e.g. 0xFFFFFFFC) to discover memory topology — under
    // KPX these reads SIGSEGV and the handler skips the instruction. Under
    // Unicorn we dynamically map a zero-filled 4 KB page covering the probe
    // address and return true so the access retries successfully, matching
    // KPX's "read returns 0" behaviour. FETCH faults stay fatal.
    {
        static auto mem_unmapped_cb = [](uc_engine *uc, uc_mem_type type,
                                         uint64_t addr, int size,
                                         int64_t value, void *) -> bool {
            (void)value; (void)size;
            uint32_t pc = 0;
            uc_reg_read(uc, UC_PPC_REG_PC, &pc);

            if (type == UC_MEM_FETCH_UNMAPPED) {
                fprintf(stderr, "[Unicorn-PPC] FETCH UNMAPPED @pc=0x%08x "
                                "addr=0x%010llx (fatal)\n",
                        pc, (unsigned long long)addr);
                g_stop_requested = true;
                uc_emu_stop(uc);
                return false;
            }

            uint64_t page = addr & ~0xFFFull;
            uint32_t perms = UC_PROT_READ | UC_PROT_WRITE;
            uc_err e = uc_mem_map(uc, page, 0x1000, perms);
            fprintf(stderr, "[Unicorn-PPC] UNMAPPED %s @pc=0x%08x "
                            "addr=0x%010llx size=%d — mapped zero page @0x%010llx (%s)\n",
                    (type == UC_MEM_READ_UNMAPPED) ? "READ" : "WRITE",
                    pc, (unsigned long long)addr, size,
                    (unsigned long long)page, uc_strerror(e));
            if (e != UC_ERR_OK) {
                g_stop_requested = true;
                uc_emu_stop(uc);
                return false;
            }
            return true;
        };
        uc_hook hook_unmapped = 0;
        uc_hook_add(g_uc, &hook_unmapped,
                    UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                    UC_HOOK_MEM_FETCH_UNMAPPED,
                    (void *)(bool (*)(uc_engine *, uc_mem_type, uint64_t,
                                      int, int64_t, void *))mem_unmapped_cb,
                    nullptr, 1, 0);
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

    // DR Emulator / DR Cache — see §3: KPX unmaps these so SIGSEGV skip-insn
    // drives nanokernel down the right init path, then remaps as RAM after
    // first IRQ. Unicorn can't cleanly skip instructions from
    // UC_HOOK_MEM_UNMAPPED, so we map them RW up front (zeros on first read).
    // Revisit with uc_mmio_map if boot diverges at probe points.
    if (!map_region(g_uc, DR_EMUL_BASE, DR_EMUL_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    (void *)(uintptr_t)DR_EMUL_BASE, "DR_emul"))
        goto fail;
    if (!map_region(g_uc, DR_CACHE_BASE, DR_CACHE_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    (void *)(uintptr_t)DR_CACHE_BASE, "DR_cache"))
        goto fail;

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
    uint32_t stuck_pc = 0;
    int stuck_count = 0;
    while (!g_stop_requested) {
        uint32_t pc = rd_pc();
        uc_err err = uc_emu_start(g_uc, pc, 0, /*timeout*/0, /*count*/0);
        if (err != UC_ERR_OK) {
            uint32_t after = rd_pc();
            fprintf(stderr, "[Unicorn-PPC] uc_emu_start err=%s pc=0x%08x\n",
                    uc_strerror(err), after);
            if (after == stuck_pc) {
                if (++stuck_count >= 3) {
                    fprintf(stderr, "[Unicorn-PPC] stuck at pc=0x%08x — bailing out\n",
                            after);
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

    uc_err err = uc_emu_start(g_uc, entry, 0, 0, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[Unicorn-PPC] execute_ppc(0x%08x) err=%s pc=0x%08x\n",
                entry, uc_strerror(err), rd_pc());
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

// These are stubs because the ppc_emulop path routes directly through the
// shared g_platform.ppc_emulop_handler (set by emul_op_ppc.cpp's init).
static void uppc_ppc_emulop_handler(void *, uint32_t, int) {}
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

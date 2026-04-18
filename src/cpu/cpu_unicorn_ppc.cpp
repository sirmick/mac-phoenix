// Unicorn PPC backend — initial implementation.
//
// Alternative PPC backend using Unicorn Engine's TCG-based PPC target instead
// of KPX's SheepShaver-derived interpreter. Coexists with KPX and is selected
// via `--backend unicorn --arch ppc`.
//
// Current scope (milestone 1 of docs/ppc/UnicornPpcPlan.md):
//   - Memory accessors working via REAL_ADDRESSING (so InitAll_PPC runs).
//   - Open Unicorn, map memory to match KPX's layout.
//   - Set initial CPU state (PC, GPR3, GPR4) the same way KPX does.
//   - Run uc_emu_start with a small instruction budget and log what happens.
//
// Not yet wired:
//   - EmulOp dispatch (major opcode 6) — requires translate.c patch (§5).
//   - IRQ injection + handshake (§8).
//   - Execute-68k-from-PPC path (§5 EXEC_NATIVE, §2 trampoline).
//   - uc_mmio_map for 0x68070000 / 0x69000000 probe addresses (§3).

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "platform.h"
#include "unicorn/unicorn.h"

namespace ppc {
    extern uint32_t RAMBase, RAMSize, ROMBase, KernelDataAddr;
    extern uint8_t *RAMBaseHost, *ROMBaseHost;
}

// XLM run-mode constants (match src/cpu/kpx/compat/xlowmem.h).
#ifndef XLM_RUN_MODE
#define XLM_RUN_MODE      0x68ffec00
#endif
#ifndef MODE_68K
#define MODE_68K          0
#endif

// Nanokernel probe regions — unmapped during init so nanokernel faults, then
// mapped after first IRQ. KPX relies on host SIGSEGV skip; Unicorn cannot
// cleanly skip an instruction from UC_HOOK_MEM_UNMAPPED, so we will hook
// these as zero-returning mmio regions in a later increment (§3).

extern "C" {

// -----------------------------------------------------------------------------
// Memory accessors — REAL_ADDRESSING, Mac addr == host addr, big-endian guest.
// These work immediately (no Unicorn dependency) so InitAll_PPC can use them
// from the install site forward.
// -----------------------------------------------------------------------------

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

static uint8_t *uppc_mem_mac_to_host(uint32_t addr)
{
    return (uint8_t *)(uintptr_t)addr;
}
static uint32_t uppc_mem_host_to_mac(uint8_t *ptr)
{
    return (uint32_t)(uintptr_t)ptr;
}

// -----------------------------------------------------------------------------
// Unicorn engine state.
// -----------------------------------------------------------------------------

static uc_engine *g_uc = nullptr;
static volatile bool g_stop_requested = false;

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

// -----------------------------------------------------------------------------
// Platform API — lifecycle.
// -----------------------------------------------------------------------------

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

    // Select CPU model (matches KPX PVR 0x000c0000 = PowerPC 750 / G3).
    err = uc_ctl_set_cpu_model(g_uc, UC_CPU_PPC32_750_V3_1);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[Unicorn-PPC] uc_ctl_set_cpu_model(PPC32_750_V3_1) failed: %s\n",
                uc_strerror(err));
        // Non-fatal for now — default model may still run.
    }

    // -------------------------------------------------------------------------
    // Memory map — mirror KPX's mmap layout at byte-for-byte addresses.
    // -------------------------------------------------------------------------

    // RAM + nanokernel probe pad. RAMBaseHost is already mmap'd at address 0
    // with an 8 MB tail (see src/core/cpu_context.cpp:init_ppc).
    const size_t NK_PROBE_PAD = 8 * 1024 * 1024;
    if (!map_region(g_uc, RAMBase, RAMSize + NK_PROBE_PAD,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    RAMBaseHost, "RAM+NKpad")) {
        uc_close(g_uc); g_uc = nullptr; return false;
    }

    // ROM area: 5 MB at 0x50000000.
    const size_t ROM_AREA_SIZE = 0x500000;
    if (!map_region(g_uc, ROMBase, ROM_AREA_SIZE,
                    UC_PROT_READ | UC_PROT_EXEC,
                    ROMBaseHost, "ROM")) {
        uc_close(g_uc); g_uc = nullptr; return false;
    }

    // KernelData aliases — single SHM, mapped at two Mac addresses. Both
    // Mac addresses already point at the same host pointer because
    // init_ppc shmat'd the same segment at both (VMBaseDiff=0).
    const uint32_t KD_SIZE = 0x2000;
    if (!map_region(g_uc, 0x68ffe000, KD_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE,
                    (void *)(uintptr_t)0x68ffe000, "KD_hi")) {
        uc_close(g_uc); g_uc = nullptr; return false;
    }
    if (!map_region(g_uc, 0x5fffe000, KD_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE,
                    (void *)(uintptr_t)0x5fffe000, "KD_lo")) {
        uc_close(g_uc); g_uc = nullptr; return false;
    }

    // DR Emulator (0x68070000) and DR Cache (0x69000000) are mapped in
    // cpu_context.cpp after InitAll_PPC. By the time cpu_init runs they are
    // already RW mmap'd. KPX relied on them being absent during nanokernel
    // init (first fault triggers the right path); since Unicorn cannot skip
    // via UC_HOOK_MEM_UNMAPPED cleanly, we map them RW here and let the
    // nanokernel see zeros on first touch, matching current init_ppc state.
    // If that diverges from KPX boot, switch to uc_mmio_map returning zero
    // until first IRQ, then remap as RAM (§3 of the plan).
    const uint32_t DR_EMUL_BASE = 0x68070000, DR_EMUL_SIZE = 0x10000;
    const uint32_t DR_CACH_BASE = 0x69000000, DR_CACH_SIZE = 0x80000;
    if (!map_region(g_uc, DR_EMUL_BASE, DR_EMUL_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    (void *)(uintptr_t)DR_EMUL_BASE, "DR_emul")) {
        uc_close(g_uc); g_uc = nullptr; return false;
    }
    if (!map_region(g_uc, DR_CACH_BASE, DR_CACH_SIZE,
                    UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC,
                    (void *)(uintptr_t)DR_CACH_BASE, "DR_cache")) {
        uc_close(g_uc); g_uc = nullptr; return false;
    }

    // -------------------------------------------------------------------------
    // Initial CPU state — matches KPX's kpx_cpu_init.
    // -------------------------------------------------------------------------

    const uint32_t entry  = ROMBase + 0x310000;
    const uint32_t gpr3   = ROMBase + 0x30d000;
    const uint32_t gpr4   = KernelDataAddr + 0x1000;

    uint32_t v;
    v = entry;  uc_reg_write(g_uc, UC_PPC_REG_PC, &v);
    v = gpr3;   uc_reg_write(g_uc, UC_PPC_REG_3,  &v);
    v = gpr4;   uc_reg_write(g_uc, UC_PPC_REG_4,  &v);

    uppc_mem_write_long(XLM_RUN_MODE, MODE_68K);

    fprintf(stderr, "[Unicorn-PPC] cpu_init: entry=0x%08x gpr3=0x%08x gpr4=0x%08x\n",
            entry, gpr3, gpr4);
    return true;
}

static void uppc_cpu_reset(void)                     {}
static void uppc_cpu_set_type(int, int)              {}

static int  uppc_cpu_execute_one(void)               { return 0; }

// Minimal execute loop: run until any stop event, log, return.
// Next increments will add tick thread + IRQ handshake + EmulOp dispatch.
static void uppc_cpu_execute_fast(void)
{
    if (!g_uc) {
        fprintf(stderr, "[Unicorn-PPC] execute_fast: engine not initialized\n");
        return;
    }

    uint32_t pc_before = 0;
    uc_reg_read(g_uc, UC_PPC_REG_PC, &pc_before);
    fprintf(stderr, "[Unicorn-PPC] execute_fast: starting at PC=0x%08x\n", pc_before);

    // Cap the initial run so an engine bug can't spin forever. No timeout,
    // but bound to 1000 instructions for milestone 1 (first-100-insn match).
    uc_err err = uc_emu_start(g_uc, pc_before, 0, /*timeout*/0, /*count*/1000);

    uint32_t pc_after = 0;
    uc_reg_read(g_uc, UC_PPC_REG_PC, &pc_after);
    fprintf(stderr, "[Unicorn-PPC] execute_fast: stopped at PC=0x%08x (err=%s)\n",
            pc_after, uc_strerror(err));

    g_stop_requested = true;
}

static void uppc_cpu_request_stop(void)
{
    g_stop_requested = true;
    if (g_uc) uc_emu_stop(g_uc);
}

// -----------------------------------------------------------------------------
// State query.
// -----------------------------------------------------------------------------

static uint32_t uppc_cpu_get_pc(void)
{
    uint32_t v = 0;
    if (g_uc) uc_reg_read(g_uc, UC_PPC_REG_PC, &v);
    return v;
}

static uint16_t uppc_cpu_get_sr(void)                { return 0; }
static uint32_t uppc_cpu_get_dreg(int)               { return 0; }
static uint32_t uppc_cpu_get_areg(int)               { return 0; }

static uint32_t uppc_cpu_get_gpr(int n)
{
    uint32_t v = 0;
    if (g_uc && n >= 0 && n < 32)
        uc_reg_read(g_uc, UC_PPC_REG_0 + n, &v);
    return v;
}

static void uppc_cpu_set_gpr(int n, uint32_t val)
{
    if (g_uc && n >= 0 && n < 32)
        uc_reg_write(g_uc, UC_PPC_REG_0 + n, &val);
}

static uint32_t uppc_cpu_get_cr(void)
{
    uint32_t v = 0;
    if (g_uc) uc_reg_read(g_uc, UC_PPC_REG_CR, &v);
    return v;
}
static uint32_t uppc_cpu_get_lr(void)
{
    uint32_t v = 0;
    if (g_uc) uc_reg_read(g_uc, UC_PPC_REG_LR, &v);
    return v;
}
static uint32_t uppc_cpu_get_ctr(void)
{
    uint32_t v = 0;
    if (g_uc) uc_reg_read(g_uc, UC_PPC_REG_CTR, &v);
    return v;
}

// Run code at `entry` until it returns (EXEC_RETURN sentinel). Not yet
// implemented — needs the §2 trampoline dispatch.
static void uppc_cpu_execute_ppc(uint32_t entry)
{
    fprintf(stderr, "[Unicorn-PPC] execute_ppc(0x%08x): not yet implemented\n", entry);
}

static void uppc_cpu_trigger_interrupt(int)          {}
static void uppc_invoke_debug(void)                  {}

static void uppc_cpu_execute_68k_trap(uint16_t, struct M68kRegisters *) {}
static void uppc_cpu_execute_68k(uint32_t, struct M68kRegisters *)      {}

static void uppc_flush_code_cache(void)
{
    // uc_emu_start with count=0 and matching start/end invalidates nothing;
    // proper flush comes in when we handle mtspr BAT / tlbie hooks. For now,
    // kicking the engine is the closest approximation.
    if (g_uc) uc_emu_stop(g_uc);
}

static uint16_t uppc_make_emulop(uint16_t op)        { return op; }

static void uppc_ppc_emulop_handler(void *, uint32_t, int)  {}
static void uppc_ppc_cursor_move(uint32_t, int, int)        {}

// -----------------------------------------------------------------------------
// Install.
// -----------------------------------------------------------------------------

void cpu_unicorn_ppc_install(Platform *p)
{
    p->cpu_name = "Unicorn-PPC";
    p->use_aline_emulops = false;  // PPC uses POWERPC_EMUL_OP (0x18xxxxxx)

    // Lifecycle
    p->cpu_init = uppc_cpu_init;
    p->cpu_reset = uppc_cpu_reset;
    p->cpu_set_type = uppc_cpu_set_type;

    // Execution
    p->cpu_execute_one = uppc_cpu_execute_one;
    p->cpu_execute_fast = uppc_cpu_execute_fast;
    p->cpu_request_stop = uppc_cpu_request_stop;

    // Generic state query
    p->cpu_get_pc = uppc_cpu_get_pc;
    p->cpu_get_sr = uppc_cpu_get_sr;
    p->cpu_get_dreg = uppc_cpu_get_dreg;
    p->cpu_get_areg = uppc_cpu_get_areg;

    // PPC-specific accessors
    p->cpu_get_gpr = uppc_cpu_get_gpr;
    p->cpu_set_gpr = uppc_cpu_set_gpr;
    p->cpu_get_cr = uppc_cpu_get_cr;
    p->cpu_get_lr = uppc_cpu_get_lr;
    p->cpu_get_ctr = uppc_cpu_get_ctr;
    p->cpu_execute_ppc = uppc_cpu_execute_ppc;

    // Interrupts + debug
    p->cpu_trigger_interrupt = uppc_cpu_trigger_interrupt;
    p->invoke_debug = uppc_invoke_debug;

    // 68k execution (from PPC context, via nanokernel). Not yet implemented.
    p->cpu_execute_68k_trap = uppc_cpu_execute_68k_trap;
    p->cpu_execute_68k = uppc_cpu_execute_68k;

    // Code cache
    p->flush_code_cache = uppc_flush_code_cache;

    // Memory — REAL_ADDRESSING, works immediately.
    p->mem_read_byte = uppc_mem_read_byte;
    p->mem_read_word = uppc_mem_read_word;
    p->mem_read_long = uppc_mem_read_long;
    p->mem_write_byte = uppc_mem_write_byte;
    p->mem_write_word = uppc_mem_write_word;
    p->mem_write_long = uppc_mem_write_long;
    p->mem_mac_to_host = uppc_mem_mac_to_host;
    p->mem_host_to_mac = uppc_mem_host_to_mac;

    // EmulOp + trap + cursor (stubs until §5 is wired up)
    p->make_emulop = uppc_make_emulop;
    p->m68k_emulop_handler = nullptr;
    p->ppc_emulop_handler = uppc_ppc_emulop_handler;
    p->trap_handler = nullptr;
    p->ppc_cursor_move = uppc_ppc_cursor_move;
}

} // extern "C"

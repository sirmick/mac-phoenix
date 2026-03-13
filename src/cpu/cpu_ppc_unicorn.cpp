/*
 *  cpu_ppc_unicorn.cpp - PPC Unicorn backend for Platform API
 *
 *  Uses Unicorn Engine (QEMU TCG JIT) to emulate a PowerPC 750 (G3).
 *  Handles SHEEP opcode dispatch, interrupt delivery, and memory mapping.
 *
 *  SHEEP opcodes (0x18xxxxxx) are PPC's equivalent of M68K EmulOps.
 *  They're encoded as invalid PPC instructions that trigger host-side handlers.
 */

#include "platform.h"
#include "cpu_trace.h"
#include "../config/emulator_config.h"
#include "emul_op_ppc.h"
#include "cpu_emulation.h"
#include "m68k_registers.h"

#include <unicorn/unicorn.h>
#include <unicorn/ppc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <atomic>

// Timer polling
extern "C" uint64_t poll_timer_interrupt(void);
extern "C" void start_timer_interrupt(void);

// Memory globals (set by cpu_context.cpp init_ppc)
extern uint8_t *RAMBaseHost;
extern uint8_t *ROMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMSize;

// SHEEP opcode base (bits 31-26 = 000110 = opcode 6, which is invalid PPC)
#define SHEEP_OP_BASE  0x18000000
#define SHEEP_OP_MASK  0xFC000000
#define SHEEP_SUBOP_MASK 0x003FFFFF

// SHEEP sub-operations (lower bits)
#define SHEEP_EMUL_RETURN    0  // Return from EmulOp
#define SHEEP_EXEC_RETURN    1  // Return from Execute68k
#define SHEEP_EXEC_NATIVE    2  // Execute native routine
#define SHEEP_EMUL_OP_BASE   3  // EmulOp index starts at 3

// ========================================================================
// PPC Unicorn state
// ========================================================================

static uc_engine *s_uc = nullptr;
static std::atomic<bool> s_running{false};
static std::atomic<int> s_pending_interrupt{0};
static uint64_t s_block_count = 0;

// Performance counters
static uint64_t s_sheep_op_count = 0;
static uint64_t s_interrupt_count = 0;

// ========================================================================
// SHEEP register marshaling
// ========================================================================

/*
 *  Marshal PPC registers to M68kRegisters and dispatch a SHEEP opcode.
 *  GPR8-15  → d[0-7]
 *  GPR16-22 → a[0-6]
 *  GPR1     → a[7] (stack pointer)
 */
static bool dispatch_sheep_opcode(uc_engine *uc, uint32_t insn, uint32_t pc)
{
    uint32_t subop = insn & SHEEP_SUBOP_MASK;

    // For EMUL_RETURN/EXEC_RETURN/EXEC_NATIVE, no register marshaling needed
    if (subop < 3) {
        return ppc_sheep_dispatch(insn, nullptr, pc);
    }

    // Marshal PPC registers → M68kRegisters
    M68kRegisters r;
    memset(&r, 0, sizeof(r));

    uint32_t val;
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_PPC_REG_0 + 8 + i, &val);
        r.d[i] = val;
    }
    for (int i = 0; i < 7; i++) {
        uc_reg_read(uc, UC_PPC_REG_0 + 16 + i, &val);
        r.a[i] = val;
    }
    uc_reg_read(uc, UC_PPC_REG_0 + 1, &val);
    r.a[7] = val;

    // Dispatch
    bool handled = ppc_sheep_dispatch(insn, &r, pc);

    // Marshal results back → PPC registers
    for (int i = 0; i < 8; i++) {
        val = r.d[i];
        uc_reg_write(uc, UC_PPC_REG_0 + 8 + i, &val);
    }
    for (int i = 0; i < 7; i++) {
        val = r.a[i];
        uc_reg_write(uc, UC_PPC_REG_0 + 16 + i, &val);
    }
    val = r.a[7];
    uc_reg_write(uc, UC_PPC_REG_0 + 1, &val);

    return handled;
}

// ========================================================================
// SHEEP opcode detection hook
// ========================================================================

/*
 * Block hook: called at the start of each translation block.
 * Checks for pending interrupts and polls the timer.
 */
static void ppc_hook_block(uc_engine *uc, uint64_t address,
                           uint32_t size __attribute__((unused)),
                           void *user_data __attribute__((unused)))
{
    s_block_count++;

    // Timer polling every 4096 blocks (same frequency as M68K)
    if ((s_block_count & 0xFFF) == 0) {
        poll_timer_interrupt();
    }

    // Pending interrupt delivery
    int level = s_pending_interrupt.load(std::memory_order_relaxed);
    if (level > 0) {
        s_pending_interrupt.store(0, std::memory_order_relaxed);

        // Read MSR to check if external interrupts are enabled (bit 15 = EE)
        uint32_t msr = 0;
        uc_reg_read(uc, UC_PPC_REG_MSR, &msr);
        if (msr & (1 << 15)) {
            // Interrupts enabled — stop execution so the host can deliver
            // the interrupt through the nanokernel's interrupt handler
            uc_emu_stop(uc);
            s_interrupt_count++;
        } else {
            // Interrupts masked — re-queue
            s_pending_interrupt.store(level, std::memory_order_relaxed);
        }
    }

    // Check for SHEEP opcode at this block's address
    // PPC instructions are always 4 bytes, big-endian
    uint32_t insn = 0;
    if (uc_mem_read(uc, address, &insn, 4) == UC_ERR_OK) {
        // Byte-swap from big-endian
        insn = __builtin_bswap32(insn);

        if ((insn & SHEEP_OP_MASK) == SHEEP_OP_BASE) {
            uint32_t subop = insn & SHEEP_SUBOP_MASK;

            fprintf(stderr, "[PPC-Unicorn] SHEEP opcode 0x%08x at PC 0x%08llx (subop=%u)\n",
                    insn, (unsigned long long)address, subop);
            s_sheep_op_count++;

            // Marshal registers and dispatch
            dispatch_sheep_opcode(uc, insn, (uint32_t)address);

            // Advance PC past the SHEEP instruction
            uint32_t pc = (uint32_t)address + 4;
            uc_reg_write(uc, UC_PPC_REG_PC, &pc);
            uc_emu_stop(uc);
        }
    }
}

/*
 * Invalid instruction hook: catches SHEEP opcodes that Unicorn can't decode.
 */
static bool ppc_hook_insn_invalid(uc_engine *uc,
                                   void *user_data __attribute__((unused)))
{
    uint32_t pc = 0;
    uc_reg_read(uc, UC_PPC_REG_PC, &pc);

    uint32_t insn = 0;
    if (uc_mem_read(uc, pc, &insn, 4) != UC_ERR_OK) {
        fprintf(stderr, "[PPC-Unicorn] Cannot read instruction at PC 0x%08x\n", pc);
        return false;
    }
    insn = __builtin_bswap32(insn);

    if ((insn & SHEEP_OP_MASK) == SHEEP_OP_BASE) {
        uint32_t subop = insn & SHEEP_SUBOP_MASK;

        fprintf(stderr, "[PPC-Unicorn] SHEEP opcode 0x%08x at PC 0x%08x (subop=%u)\n",
                insn, pc, subop);
        s_sheep_op_count++;

        // Marshal registers and dispatch
        dispatch_sheep_opcode(uc, insn, pc);

        // Advance PC
        pc += 4;
        uc_reg_write(uc, UC_PPC_REG_PC, &pc);
        return true;  // continue execution
    }

    fprintf(stderr, "[PPC-Unicorn] Unhandled invalid instruction 0x%08x at PC 0x%08x\n",
            insn, pc);
    return false;  // stop execution
}

/*
 * Unmapped memory hook: emulates hardware register access.
 * SheepShaver uses SIGSEGV to catch these — we use Unicorn's memory hook.
 * Returns 0 for reads, silently drops writes (like real HW returning defaults).
 */
static bool ppc_hook_mem_unmapped(uc_engine *uc,
                                   uc_mem_type type,
                                   uint64_t address,
                                   int size,
                                   int64_t value __attribute__((unused)),
                                   void *user_data __attribute__((unused)))
{
    // Log first few accesses, then go quiet
    static int unmapped_count = 0;
    if (unmapped_count < 20) {
        uint32_t pc = 0;
        uc_reg_read(uc, UC_PPC_REG_PC, &pc);
        const char *op = (type == UC_MEM_READ_UNMAPPED) ? "read" :
                         (type == UC_MEM_WRITE_UNMAPPED) ? "write" : "fetch";
        fprintf(stderr, "[PPC-Unicorn] Unmapped %s: addr=0x%08llx size=%d PC=0x%08x\n",
                op, (unsigned long long)address, size, pc);
        unmapped_count++;
        if (unmapped_count == 20) {
            fprintf(stderr, "[PPC-Unicorn] (suppressing further unmapped access messages)\n");
        }
    }

    // Map a page at the faulting address so execution can continue.
    // Align to 4KB page boundary.
    uint64_t page_base = address & ~0xFFFULL;
    uint64_t page_size = 0x1000;  // 4KB

    // Skip if near top of 32-bit space (pre-mapped during init)
    if (page_base >= 0xFFFF0000ULL) {
        return true;  // Already mapped
    }

    uc_err err = uc_mem_map(uc, page_base, page_size, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        // Page might overlap existing mapping — try a larger alignment
        page_base = address & ~0xFFFFULL;
        page_size = 0x10000;  // 64KB
        if (page_base + page_size > 0x100000000ULL) {
            page_size = 0x100000000ULL - page_base;
        }
        err = uc_mem_map(uc, page_base, page_size, UC_PROT_ALL);
    }

    // Mapped pages are zero-initialized by Unicorn, so reads return 0
    return (err == UC_ERR_OK);
}

// ========================================================================
// Platform API implementation
// ========================================================================

static bool ppc_cpu_init(void)
{
    if (s_uc) {
        fprintf(stderr, "[PPC-Unicorn] Already initialized\n");
        return true;
    }

    // Create PPC32 big-endian engine
    uc_err err = uc_open(UC_ARCH_PPC,
                         static_cast<uc_mode>(UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN),
                         &s_uc);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[PPC-Unicorn] Failed to create engine: %s\n", uc_strerror(err));
        return false;
    }

    // Set CPU model to 750 (G3)
    uc_ctl_set_cpu_model(s_uc, UC_CPU_PPC32_750_V3_1);

    // Register hooks
    uc_hook block_hook, insn_invalid_hook;

    err = uc_hook_add(s_uc, &block_hook, UC_HOOK_BLOCK,
                      (void *)ppc_hook_block, nullptr, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[PPC-Unicorn] Failed to add block hook: %s\n", uc_strerror(err));
        uc_close(s_uc);
        s_uc = nullptr;
        return false;
    }

    err = uc_hook_add(s_uc, &insn_invalid_hook, UC_HOOK_INSN_INVALID,
                      (void *)ppc_hook_insn_invalid, nullptr, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[PPC-Unicorn] Failed to add invalid insn hook: %s\n", uc_strerror(err));
        uc_close(s_uc);
        s_uc = nullptr;
        return false;
    }

    // Unmapped memory hook — catches hardware register access
    // SheepShaver uses SIGSEGV for this; we use Unicorn's memory hook
    uc_hook mem_hook;
    err = uc_hook_add(s_uc, &mem_hook,
                      static_cast<uc_hook_type>(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED),
                      (void *)ppc_hook_mem_unmapped, nullptr, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[PPC-Unicorn] Failed to add unmapped memory hook: %s\n", uc_strerror(err));
        uc_close(s_uc);
        s_uc = nullptr;
        return false;
    }

    fprintf(stderr, "[PPC-Unicorn] Engine created (PPC750 G3)\n");
    return true;
}

static void ppc_cpu_reset(void)
{
    if (!s_uc) return;

    // Reset all GPRs to 0
    uint32_t zero = 0;
    for (int i = 0; i < 32; i++) {
        uc_reg_write(s_uc, UC_PPC_REG_0 + i, &zero);
    }

    // Reset special registers
    uc_reg_write(s_uc, UC_PPC_REG_LR, &zero);
    uc_reg_write(s_uc, UC_PPC_REG_CTR, &zero);
    uc_reg_write(s_uc, UC_PPC_REG_XER, &zero);
    uc_reg_write(s_uc, UC_PPC_REG_CR, &zero);

    // MSR: supervisor mode, big-endian, FPU enabled
    uint32_t msr = 0x00002000;  // FP bit
    uc_reg_write(s_uc, UC_PPC_REG_MSR, &msr);

    s_block_count = 0;
    s_sheep_op_count = 0;
    s_interrupt_count = 0;
    s_pending_interrupt.store(0);
    s_running.store(false);

    fprintf(stderr, "[PPC-Unicorn] CPU reset\n");
}

static void ppc_cpu_set_type(int cpu_type __attribute__((unused)),
                             int fpu_type __attribute__((unused)))
{
    // PPC always uses 750 (G3) — cpu_type/fpu_type are M68K concepts
}

static int ppc_cpu_execute_one(void)
{
    if (!s_uc) return 3;  // error

    uint32_t pc = 0;
    uc_reg_read(s_uc, UC_PPC_REG_PC, &pc);

    uc_err err = uc_emu_start(s_uc, pc, 0, 0, 1);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[PPC-Unicorn] Execute one failed: %s\n", uc_strerror(err));
        return 3;
    }
    return 0;
}

static void ppc_cpu_execute_fast(void)
{
    if (!s_uc) return;

    s_running.store(true, std::memory_order_release);
    fprintf(stderr, "[PPC-Unicorn] Starting execution loop\n");

    while (s_running.load(std::memory_order_acquire)) {
        uint32_t pc = 0;
        uc_reg_read(s_uc, UC_PPC_REG_PC, &pc);

        uc_err err = uc_emu_start(s_uc, pc, 0, 0, 0);
        if (err != UC_ERR_OK && s_running.load()) {
            fprintf(stderr, "[PPC-Unicorn] Execution error at PC 0x%08x: %s\n",
                    pc, uc_strerror(err));

            // Log CPU state for debugging
            uint32_t lr = 0, ctr = 0, msr = 0, cr = 0;
            uc_reg_read(s_uc, UC_PPC_REG_LR, &lr);
            uc_reg_read(s_uc, UC_PPC_REG_CTR, &ctr);
            uc_reg_read(s_uc, UC_PPC_REG_MSR, &msr);
            uc_reg_read(s_uc, UC_PPC_REG_CR, &cr);
            fprintf(stderr, "[PPC-Unicorn] LR=0x%08x CTR=0x%08x MSR=0x%08x CR=0x%08x\n",
                    lr, ctr, msr, cr);

            // Print GPRs
            for (int i = 0; i < 32; i += 4) {
                uint32_t r[4];
                for (int j = 0; j < 4; j++)
                    uc_reg_read(s_uc, UC_PPC_REG_0 + i + j, &r[j]);
                fprintf(stderr, "[PPC-Unicorn] r%d-r%d: 0x%08x 0x%08x 0x%08x 0x%08x\n",
                        i, i+3, r[0], r[1], r[2], r[3]);
            }
            break;
        }
    }

    fprintf(stderr, "[PPC-Unicorn] Execution stopped (blocks=%llu, sheep=%llu, irq=%llu)\n",
            (unsigned long long)s_block_count,
            (unsigned long long)s_sheep_op_count,
            (unsigned long long)s_interrupt_count);
}

static void ppc_cpu_request_stop(void)
{
    s_running.store(false, std::memory_order_release);
    if (s_uc) {
        uc_emu_stop(s_uc);
    }
}

static uint32_t ppc_cpu_get_pc(void)
{
    uint32_t pc = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_PC, &pc);
    return pc;
}

static uint16_t ppc_cpu_get_sr(void)
{
    // PPC doesn't have SR — return MSR truncated (for compatibility)
    uint32_t msr = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_MSR, &msr);
    return (uint16_t)(msr & 0xFFFF);
}

static uint32_t ppc_cpu_get_dreg(int n)
{
    // PPC doesn't have D registers — map to GPR for compatibility
    // (68k d0-d7 shadow GPR8-15 in SheepShaver's DR emulator)
    uint32_t val = 0;
    if (s_uc && n >= 0 && n <= 7) {
        uc_reg_read(s_uc, UC_PPC_REG_8 + n, &val);
    }
    return val;
}

static uint32_t ppc_cpu_get_areg(int n)
{
    // PPC doesn't have A registers — map to GPR for compatibility
    // (68k a0-a6 shadow GPR16-22, a7/sp = GPR1)
    uint32_t val = 0;
    if (s_uc) {
        if (n == 7) {
            uc_reg_read(s_uc, UC_PPC_REG_1, &val);  // SP
        } else if (n >= 0 && n <= 6) {
            uc_reg_read(s_uc, UC_PPC_REG_16 + n, &val);
        }
    }
    return val;
}

static void ppc_cpu_trigger_interrupt(int level __attribute__((unused)))
{
    s_pending_interrupt.store(1, std::memory_order_release);
    if (s_uc) uc_emu_stop(s_uc);
}

static void ppc_cpu_execute_68k_trap(uint16_t trap __attribute__((unused)),
                                     struct M68kRegisters *r __attribute__((unused)))
{
    fprintf(stderr, "[PPC-Unicorn] execute_68k_trap not implemented\n");
}

static void ppc_cpu_execute_68k(uint32_t addr __attribute__((unused)),
                                struct M68kRegisters *r __attribute__((unused)))
{
    fprintf(stderr, "[PPC-Unicorn] execute_68k not implemented\n");
}

static void ppc_flush_code_cache(void)
{
    if (s_uc) {
        uc_ctl_remove_cache(s_uc, 0, ~(uint64_t)0);
    }
}

// ========================================================================
// PPC-specific Platform API (register accessors)
// ========================================================================

static uint32_t ppc_get_gpr(int n)
{
    uint32_t val = 0;
    if (s_uc && n >= 0 && n <= 31)
        uc_reg_read(s_uc, UC_PPC_REG_0 + n, &val);
    return val;
}

static void ppc_set_gpr(int n, uint32_t val)
{
    if (s_uc && n >= 0 && n <= 31)
        uc_reg_write(s_uc, UC_PPC_REG_0 + n, &val);
}

static uint32_t ppc_get_pc(void) { return ppc_cpu_get_pc(); }

static void ppc_set_pc(uint32_t val)
{
    if (s_uc) uc_reg_write(s_uc, UC_PPC_REG_PC, &val);
}

static uint32_t ppc_get_lr(void)
{
    uint32_t val = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_LR, &val);
    return val;
}

static void ppc_set_lr(uint32_t val)
{
    if (s_uc) uc_reg_write(s_uc, UC_PPC_REG_LR, &val);
}

static uint32_t ppc_get_ctr(void)
{
    uint32_t val = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_CTR, &val);
    return val;
}

static void ppc_set_ctr(uint32_t val)
{
    if (s_uc) uc_reg_write(s_uc, UC_PPC_REG_CTR, &val);
}

static uint32_t ppc_get_cr(void)
{
    uint32_t val = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_CR, &val);
    return val;
}

static void ppc_set_cr(uint32_t val)
{
    if (s_uc) uc_reg_write(s_uc, UC_PPC_REG_CR, &val);
}

static uint32_t ppc_get_msr(void)
{
    uint32_t val = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_MSR, &val);
    return val;
}

static void ppc_set_msr(uint32_t val)
{
    if (s_uc) uc_reg_write(s_uc, UC_PPC_REG_MSR, &val);
}

static uint32_t ppc_get_xer(void)
{
    uint32_t val = 0;
    if (s_uc) uc_reg_read(s_uc, UC_PPC_REG_XER, &val);
    return val;
}

static void ppc_set_xer(uint32_t val)
{
    if (s_uc) uc_reg_write(s_uc, UC_PPC_REG_XER, &val);
}

static void ppc_execute(void)
{
    ppc_cpu_execute_fast();
}

static void ppc_stop(void)
{
    ppc_cpu_request_stop();
}

static void ppc_interrupt(uint32_t type __attribute__((unused)))
{
    ppc_cpu_trigger_interrupt(1);
}

// ========================================================================
// Memory System API
// ========================================================================

static uint8_t ppc_mem_read_byte(uint32_t addr)
{
    if (RAMBaseHost && addr < RAMSize) return RAMBaseHost[addr];
    if (ROMBaseHost && addr >= 0x400000 && addr < 0x400000 + ROMSize)
        return ROMBaseHost[addr - 0x400000];
    return 0;
}

static uint16_t ppc_mem_read_word(uint32_t addr)
{
    uint8_t *p = nullptr;
    if (RAMBaseHost && addr < RAMSize) p = RAMBaseHost + addr;
    else if (ROMBaseHost && addr >= 0x400000 && addr < 0x400000 + ROMSize)
        p = ROMBaseHost + (addr - 0x400000);
    if (p) return (p[0] << 8) | p[1];
    return 0;
}

static uint32_t ppc_mem_read_long(uint32_t addr)
{
    uint8_t *p = nullptr;
    if (RAMBaseHost && addr < RAMSize) p = RAMBaseHost + addr;
    else if (ROMBaseHost && addr >= 0x400000 && addr < 0x400000 + ROMSize)
        p = ROMBaseHost + (addr - 0x400000);
    if (p) return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    return 0;
}

static void ppc_mem_write_byte(uint32_t addr, uint8_t val)
{
    if (RAMBaseHost && addr < RAMSize) RAMBaseHost[addr] = val;
}

static void ppc_mem_write_word(uint32_t addr, uint16_t val)
{
    if (RAMBaseHost && addr < RAMSize) {
        RAMBaseHost[addr] = val >> 8;
        RAMBaseHost[addr + 1] = val & 0xFF;
    }
}

static void ppc_mem_write_long(uint32_t addr, uint32_t val)
{
    if (RAMBaseHost && addr < RAMSize) {
        RAMBaseHost[addr] = (val >> 24) & 0xFF;
        RAMBaseHost[addr + 1] = (val >> 16) & 0xFF;
        RAMBaseHost[addr + 2] = (val >> 8) & 0xFF;
        RAMBaseHost[addr + 3] = val & 0xFF;
    }
}

static uint8_t *ppc_mem_mac_to_host(uint32_t addr)
{
    if (RAMBaseHost && addr < RAMSize) return RAMBaseHost + addr;
    if (ROMBaseHost && addr >= 0x400000 && addr < 0x400000 + ROMSize)
        return ROMBaseHost + (addr - 0x400000);
    return nullptr;
}

static uint32_t ppc_mem_host_to_mac(uint8_t *ptr)
{
    if (RAMBaseHost && ptr >= RAMBaseHost && ptr < RAMBaseHost + RAMSize)
        return (uint32_t)(ptr - RAMBaseHost);
    if (ROMBaseHost && ptr >= ROMBaseHost && ptr < ROMBaseHost + ROMSize)
        return 0x400000 + (uint32_t)(ptr - ROMBaseHost);
    return 0;
}

// ========================================================================
// Map memory into Unicorn engine
// ========================================================================

bool ppc_unicorn_map_memory(void)
{
    if (!s_uc) return false;

    // Map RAM at 0x0
    if (RAMBaseHost && RAMSize > 0) {
        uc_err err = uc_mem_map_ptr(s_uc, 0, RAMSize, UC_PROT_ALL, RAMBaseHost);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "[PPC-Unicorn] Failed to map RAM: %s\n", uc_strerror(err));
            return false;
        }
        fprintf(stderr, "[PPC-Unicorn] RAM mapped: 0x0 - 0x%x (%u MB)\n",
                RAMSize, RAMSize / (1024*1024));
    }

    // ROM is loaded into RAM at 0x400000 by init_ppc, so it's already mapped
    // via the RAM mapping above. For read-only protection we could remap that
    // region, but for now it's fine as RW (ROM patching needs write access).

    // Map Kernel Data area at 0x68FFE000 (8KB)
    // This is outside RAM range and needs its own mapping.
    // Also map surrounding area for DR Emulator (0x68070000) and DR Cache (0x69000000)
    // We map a contiguous 16MB block at 0x68000000 to cover all of:
    //   - DR Emulator at 0x68070000 (64KB)
    //   - Kernel Data  at 0x68FFE000 (8KB)
    //   - DR Cache     at 0x69000000 (512KB)
    {
        const uint64_t kd_region_base = 0x68000000;
        const uint64_t kd_region_size = 0x02000000;  // 32MB covers up to 0x6A000000
        uc_err err = uc_mem_map(s_uc, kd_region_base, kd_region_size, UC_PROT_ALL);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "[PPC-Unicorn] Failed to map Kernel Data region: %s\n", uc_strerror(err));
            return false;
        }
        fprintf(stderr, "[PPC-Unicorn] Kernel Data region mapped: 0x%llx - 0x%llx (32 MB)\n",
                (unsigned long long)kd_region_base,
                (unsigned long long)(kd_region_base + kd_region_size));
    }

    // Map Kernel Data 2 at 0x5FFFE000 (8KB) — alternate address
    {
        const uint64_t kd2_base = 0x5FFFE000;
        const uint64_t kd2_size = 0x2000;  // 8KB, page-aligned
        uc_err err = uc_mem_map(s_uc, kd2_base, kd2_size, UC_PROT_ALL);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "[PPC-Unicorn] Failed to map Kernel Data 2: %s\n", uc_strerror(err));
            // Not fatal — some ROMs don't use it
        } else {
            fprintf(stderr, "[PPC-Unicorn] Kernel Data 2 mapped: 0x%llx (8 KB)\n",
                    (unsigned long long)kd2_base);
        }
    }

    // Map top of 32-bit address space (PPC reset vector area)
    // The nanokernel reads from 0xFFFFFFFC (hardware reset vector on real PPC).
    // On real hardware this is ROM aliased to the top of address space.
    {
        const uint64_t top_base = 0xFFFF0000;
        const uint64_t top_size = 0x10000;  // 64KB to end of 32-bit space
        uc_err err = uc_mem_map(s_uc, top_base, top_size, UC_PROT_ALL);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "[PPC-Unicorn] Warning: Failed to map reset vector area: %s\n",
                    uc_strerror(err));
        } else {
            fprintf(stderr, "[PPC-Unicorn] Reset vector area mapped: 0xFFFF0000 (64 KB)\n");
        }
    }

    // Initialize Kernel Data structure at 0x68FFE000
    // This must be done after mapping since it's outside RAM
    {
        const uint64_t kd_base = 0x68FFE000;

        // Clear 8KB
        uint8_t zeros[0x2000] = {0};
        uc_mem_write(s_uc, kd_base, zeros, sizeof(zeros));

        // Helper to write big-endian uint32 into Unicorn memory
        auto write_be32 = [&](uint64_t addr, uint32_t val) {
            uint32_t be = __builtin_bswap32(val);
            uc_mem_write(s_uc, addr, &be, 4);
        };

        // Fill in ROM-type-specific fields (based on SheepShaver main.cpp InitAll)
        // 0xb80..0xbff: ROM base, device tree, vector tables
        // Fill 0xb80..0xc00 with 0x3d (like SheepShaver does)
        uint8_t fill_3d[0x80];
        memset(fill_3d, 0x3d, sizeof(fill_3d));
        uc_mem_write(s_uc, kd_base + 0xb80, fill_3d, sizeof(fill_3d));

        uint32_t rom_base = 0x00400000;  // PPC_ROM_BASE

        write_be32(kd_base + 0xb80, rom_base);          // ROM base
        write_be32(kd_base + 0xb84, 0);                 // OF device tree (none)
        write_be32(kd_base + 0xb90, 0);                 // Vector lookup table (none)
        write_be32(kd_base + 0xb94, 0);                 // Vector mask table (none)
        write_be32(kd_base + 0xb98, rom_base);          // OpenPIC base
        write_be32(kd_base + 0xbb0, 0);                 // ADB base
        write_be32(kd_base + 0xc20, RAMSize);            // RAM size
        write_be32(kd_base + 0xc24, RAMSize);            // RAM size (again)
        write_be32(kd_base + 0xc30, RAMSize);            // RAM size (for memory manager)
        write_be32(kd_base + 0xc34, RAMSize);
        write_be32(kd_base + 0xc50, RAMSize);
        write_be32(kd_base + 0xc54, RAMSize);

        // Processor info
        write_be32(kd_base + 0xf60, 0x00080301);        // PVR (750 v3.1)
        write_be32(kd_base + 0xf64, 266000000);         // CPU clock (266 MHz)
        write_be32(kd_base + 0xf68, 66000000);           // Bus clock (66 MHz)
        write_be32(kd_base + 0xf6c, 16625000);           // Timebase (bus/4)

        fprintf(stderr, "[PPC-Unicorn] Kernel Data initialized at 0x%llx\n",
                (unsigned long long)kd_base);
    }

    fprintf(stderr, "[PPC-Unicorn] Memory mapping complete\n");
    return true;
}

// ========================================================================
// Install function
// ========================================================================

extern "C" void cpu_ppc_unicorn_install(Platform *p)
{
    fprintf(stderr, "[PPC-Unicorn] Installing PPC Unicorn backend\n");

    p->cpu_name = "PPC-Unicorn";
    p->use_aline_emulops = false;  // PPC uses SHEEP opcodes, not A-line

    // CPU lifecycle
    p->cpu_init = ppc_cpu_init;
    p->cpu_reset = ppc_cpu_reset;
    p->cpu_set_type = ppc_cpu_set_type;

    // Execution
    p->cpu_execute_one = ppc_cpu_execute_one;
    p->cpu_execute_fast = ppc_cpu_execute_fast;
    p->cpu_request_stop = ppc_cpu_request_stop;

    // M68K-compatible state query (maps to PPC equivalents)
    p->cpu_get_pc = ppc_cpu_get_pc;
    p->cpu_get_sr = ppc_cpu_get_sr;
    p->cpu_get_dreg = ppc_cpu_get_dreg;
    p->cpu_get_areg = ppc_cpu_get_areg;

    // Interrupts
    p->cpu_trigger_interrupt = ppc_cpu_trigger_interrupt;

    // 68k execution (needed for mixed-mode)
    p->cpu_execute_68k_trap = ppc_cpu_execute_68k_trap;
    p->cpu_execute_68k = ppc_cpu_execute_68k;

    // Code cache
    p->flush_code_cache = ppc_flush_code_cache;

    // Memory system
    p->mem_read_byte = ppc_mem_read_byte;
    p->mem_read_word = ppc_mem_read_word;
    p->mem_read_long = ppc_mem_read_long;
    p->mem_write_byte = ppc_mem_write_byte;
    p->mem_write_word = ppc_mem_write_word;
    p->mem_write_long = ppc_mem_write_long;
    p->mem_mac_to_host = ppc_mem_mac_to_host;
    p->mem_host_to_mac = ppc_mem_host_to_mac;

    // PPC-specific register accessors
    p->cpu_ppc_get_gpr = ppc_get_gpr;
    p->cpu_ppc_set_gpr = ppc_set_gpr;
    p->cpu_ppc_get_pc = ppc_get_pc;
    p->cpu_ppc_set_pc = ppc_set_pc;
    p->cpu_ppc_get_lr = ppc_get_lr;
    p->cpu_ppc_set_lr = ppc_set_lr;
    p->cpu_ppc_get_ctr = ppc_get_ctr;
    p->cpu_ppc_set_ctr = ppc_set_ctr;
    p->cpu_ppc_get_cr = ppc_get_cr;
    p->cpu_ppc_set_cr = ppc_set_cr;
    p->cpu_ppc_get_msr = ppc_get_msr;
    p->cpu_ppc_set_msr = ppc_set_msr;
    p->cpu_ppc_get_xer = ppc_get_xer;
    p->cpu_ppc_set_xer = ppc_set_xer;

    // PPC execution
    p->cpu_ppc_execute = ppc_execute;
    p->cpu_ppc_stop = ppc_stop;
    p->cpu_ppc_interrupt = ppc_interrupt;
}

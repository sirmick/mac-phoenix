/**
 * Unicorn Engine Wrapper Implementation (Cleaned Version)
 *
 * Provides M68K emulation using Unicorn engine with A-line EmulOp support.
 * All obsolete MMIO transport code has been removed.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "unicorn_wrapper.h"
#include "platform.h"
#include "cpu_trace.h"
#include <stdlib.h>  /* For strtoul */
#include "timer_interrupt.h"
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Forward declarations for EmulOp interface */
struct M68kRegistersC {
    uint32_t d[8];  /* Data registers D0-D7 */
    uint32_t a[8];  /* Address registers A0-A7 */
    uint16_t sr;    /* Status register */
};
extern void EmulOp_C(uint16_t opcode, struct M68kRegistersC *r);

/* M68kRegisters is same as M68kRegistersC for our purposes */
#define M68kRegisters M68kRegistersC

/* M68K interrupt trigger (from Unicorn's QEMU backend) */
extern void uc_m68k_trigger_interrupt(uc_engine *uc, int level, uint8_t vector);

/* Interrupt handling */
volatile int g_pending_interrupt_level = 0;

/* Flag: Execute68kTrap return sentinel was hit */
volatile bool g_exec68k_return_flag = false;

/* Flag: interrupt was triggered in QEMU, waiting for handler to start.
 * After we call uc_m68k_trigger_interrupt() and uc_emu_stop(), QEMU will
 * deliver the interrupt on the next uc_emu_start(). The first hook_block
 * after that will be in the interrupt handler (with IPL raised). At that
 * point we clear QEMU's pending_level to prevent re-delivery after RTE.
 * This mimics the hardware interrupt acknowledge cycle. */
static bool g_interrupt_pending_ack = false;

/* IRQ handler trace: log blocks executed during interrupt handler */
static int g_irq_trace_blocks_remaining = 0;
static int g_irq_trace_count = 0;  /* how many full traces we've done */

/* Block statistics for timing analysis */
typedef struct {
    uint64_t total_blocks;
    uint64_t total_instructions;
    uint64_t block_size_histogram[101];
    uint32_t min_block_size;
    uint32_t max_block_size;
    uint64_t sum_block_sizes;
} BlockStats;

struct UnicornCPU {
    uc_engine *uc;
    UnicornArch arch;
    char error[256];

    /* Memory hook callback */
    MemoryHookCallback memory_hook;
    void *memory_user_data;
    uc_hook mem_hook_handle;

    /* Hooks */
    uc_hook block_hook;        /* UC_HOOK_BLOCK for interrupts */
    uc_hook insn_invalid_hook; /* UC_HOOK_INSN_INVALID for legacy EmulOps */
    uc_hook intr_hook;         /* UC_HOOK_INTR for A-line EmulOps */
    uc_hook trace_hook;        /* UC_HOOK_MEM_READ for CPU tracing */

    /* Block statistics */
    BlockStats block_stats;

    /* Deferred SR update */
    bool has_deferred_sr_update;
    uint16_t deferred_sr_value;

    /* Deferred register updates (D0-D7, A0-A7) */
    bool has_deferred_dreg_update[8];
    uint32_t deferred_dreg_value[8];
    bool has_deferred_areg_update[8];
    uint32_t deferred_areg_value[8];

    /* PC-based tracing state */
    bool pc_trace_enabled;
};

/* Deferred SR update API */
void unicorn_defer_sr_update(void *unicorn_cpu, uint16_t new_sr) {
    UnicornCPU *cpu = (UnicornCPU *)unicorn_cpu;
    if (cpu) {
        cpu->has_deferred_sr_update = true;
        cpu->deferred_sr_value = new_sr;
    }
}

/* Deferred D register update API */
void unicorn_defer_dreg_update(void *unicorn_cpu, int reg, uint32_t value) {
    UnicornCPU *cpu = (UnicornCPU *)unicorn_cpu;
    if (cpu && reg >= 0 && reg <= 7) {
        cpu->has_deferred_dreg_update[reg] = true;
        cpu->deferred_dreg_value[reg] = value;
    }
}

/* Deferred A register update API */
void unicorn_defer_areg_update(void *unicorn_cpu, int reg, uint32_t value) {
    UnicornCPU *cpu = (UnicornCPU *)unicorn_cpu;
    if (cpu && reg >= 0 && reg <= 7) {
        cpu->has_deferred_areg_update[reg] = true;
        cpu->deferred_areg_value[reg] = value;
    }
}

/**
 * Helper: Apply all deferred register updates and flush translation cache
 *
 * This centralizes the cache flushing logic required after register updates.
 * Per Unicorn FAQ: "any operation on cached addresses won't immediately
 * take effect without a call to uc_ctl_remove_cache"
 *
 * Returns: true if any updates were applied (and cache was flushed)
 */
static bool apply_deferred_updates_and_flush(UnicornCPU *cpu, uc_engine *uc, const char *caller) {
    if (!cpu || !uc) return false;

    bool any_updates = false;

    /* Apply deferred SR update */
    if (cpu->has_deferred_sr_update) {
        /* uc_reg_write expects uint32_t* for SR */
        uint32_t sr32 = cpu->deferred_sr_value;
        uc_reg_write(uc, UC_M68K_REG_SR, &sr32);
        cpu->has_deferred_sr_update = false;
        any_updates = true;
    }

    /* Apply deferred D register updates */
    for (int i = 0; i < 8; i++) {
        if (cpu->has_deferred_dreg_update[i]) {
            uc_reg_write(uc, UC_M68K_REG_D0 + i, &cpu->deferred_dreg_value[i]);
            cpu->has_deferred_dreg_update[i] = false;
            any_updates = true;
        }
    }

    /* Apply deferred A register updates */
    for (int i = 0; i < 8; i++) {
        if (cpu->has_deferred_areg_update[i]) {
            uc_reg_write(uc, UC_M68K_REG_A0 + i, &cpu->deferred_areg_value[i]);
            cpu->has_deferred_areg_update[i] = false;
            any_updates = true;
        }
    }

    /* IMPORTANT: Register writes do NOT require manual cache flushing!
     *
     * Research findings from Unicorn source code (uc.c):
     * - uc_reg_write() AUTOMATICALLY flushes cache when writing to PC
     * - Writing to other registers (D0-D7, A0-A7, SR) does NOT require cache flush
     * - Translation blocks (TBs) only need flushing when CODE is modified, not registers
     *
     * The Unicorn FAQ advice about "editing an instruction" applies to CODE modification,
     * not register modification. Register values are read from CPU state at runtime,
     * not baked into the translated blocks.
     *
     * Our previous implementation was doing TRIPLE flushing:
     * 1. uc_ctl_remove_cache() - manual flush (unnecessary!)
     * 2. uc_reg_write(PC) - automatic flush inside Unicorn (redundant!)
     * 3. break_translation_loop() inside uc_reg_write (redundant!)
     *
     * This caused massive performance degradation. The fix: do nothing.
     * Register updates take effect immediately without any cache management.
     */

    return any_updates;
}

/* Global flag set by EmulOp FIX_MEMSIZE to trace post-fixmem blocks */
static int g_post_fixmem_blocks = 0;

/* Diagnostic counters for interrupt flow analysis */
static uint64_t diag_timer_polls = 0;
static uint64_t diag_timer_fires = 0;
static uint64_t diag_irq_delivered = 0;
static uint64_t diag_irq_blocked = 0;

/* Circular buffer for trap dispatches - dumped on BAD PC */
#define TRAP_DISPATCH_BUF_SIZE 64
typedef struct {
    uint32_t d1;          /* D1 register (trap word) */
    uint32_t handler;     /* Handler address from trap table */
    uint32_t a7;          /* Stack pointer at dispatch */
    uint32_t a6;          /* Frame pointer at dispatch */
    uint64_t block_num;   /* Block count at time of dispatch */
    uint8_t  is_autopop;  /* 1 = auto-pop path, 0 = non-auto-pop */
} TrapDispatchEntry;
static TrapDispatchEntry trap_dispatch_buf[TRAP_DISPATCH_BUF_SIZE];
static int trap_dispatch_idx = 0;
static int trap_dispatch_total = 0;

/**
 * Hook for block execution (UC_HOOK_BLOCK)
 * Called at the start of each translation block.
 * Used for interrupt checking and block statistics.
 */
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    /* Update block statistics (cheap) */
    cpu->block_stats.total_blocks++;
    cpu->block_stats.total_instructions += size;

    /* Interrupt acknowledge: clear QEMU's pending_level on the first TB
     * after interrupt delivery. At this point the exception frame has been
     * pushed and IPL is raised, so it's safe to clear. This prevents
     * QEMU from re-delivering the same interrupt after RTE. */
    if (g_interrupt_pending_ack) {
        g_interrupt_pending_ack = false;
        static int ack_log_count = 0;
        if (++ack_log_count <= 5) {
            uint32_t ack_pc = 0, ack_sr = 0, ack_a7 = 0, ack_vbr = 0;
            uc_reg_read(uc, UC_M68K_REG_PC, &ack_pc);
            uc_reg_read(uc, UC_M68K_REG_SR, &ack_sr);
            uc_reg_read(uc, UC_M68K_REG_A7, &ack_a7);
            /* Read VBR to find where vectors live */
            uc_reg_read(uc, UC_M68K_REG_CR_VBR, &ack_vbr);
            uint8_t stk[8] = {0};
            uc_mem_read(uc, ack_a7, stk, 8);
            /* Read vector table entry for autovector 1 (offset 0x64) */
            uint8_t vec_bytes[4] = {0};
            uc_mem_read(uc, ack_vbr + 0x64, vec_bytes, 4);
            uint32_t vec_addr = (vec_bytes[0]<<24)|(vec_bytes[1]<<16)|(vec_bytes[2]<<8)|vec_bytes[3];
            /* Read first 16 bytes of code at handler */
            uint8_t handler_code[16] = {0};
            uc_mem_read(uc, ack_pc, handler_code, 16);
            fprintf(stderr, "[IRQ-ACK] #%d PC=0x%08x SR=0x%04x A7=0x%08x VBR=0x%08x "
                    "vec[0x64]=0x%08x\n"
                    "  stack=%02x%02x_%02x%02x%02x%02x_%02x%02x\n"
                    "  handler: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                    ack_log_count, ack_pc, ack_sr & 0xFFFF, ack_a7, ack_vbr, vec_addr,
                    stk[0],stk[1],stk[2],stk[3],stk[4],stk[5],stk[6],stk[7],
                    handler_code[0],handler_code[1],handler_code[2],handler_code[3],
                    handler_code[4],handler_code[5],handler_code[6],handler_code[7],
                    handler_code[8],handler_code[9],handler_code[10],handler_code[11],
                    handler_code[12],handler_code[13],handler_code[14],handler_code[15]);
        }
        uc_m68k_trigger_interrupt(uc, 0, 0);

        /* Start tracing IRQ handler blocks (first 3 interrupts only) */
        if (g_irq_trace_count < 3) {
            g_irq_trace_blocks_remaining = 100;  /* trace up to 100 blocks */
            g_irq_trace_count++;
            fprintf(stderr, "[IRQ-TRACE] Starting trace #%d of IRQ handler\n", g_irq_trace_count);
        }
    }

    /* Log blocks during IRQ handler trace */
    if (g_irq_trace_blocks_remaining > 0) {
        g_irq_trace_blocks_remaining--;
        fprintf(stderr, "[IRQ-TRACE] block=0x%08lx size=%u\n", (unsigned long)address, size);
        if (g_irq_trace_blocks_remaining == 0) {
            fprintf(stderr, "[IRQ-TRACE] Trace ended (100 blocks)\n");
        }
    }

    /* Check execution at 60Hz handler (RAM copy at 0x0200a296 or ROM at 0x4080a296) */
    if (address == 0x0200a296 || address == 0x4080a296) {
        static int rom_60hz_count = 0;
        if (++rom_60hz_count <= 5) {
            /* Read 16 bytes at the handler to see the actual instructions */
            uint8_t code[16] = {0};
            uc_mem_read(uc, address, code, 16);
            /* Also read from the ROM's version for comparison */
            uint8_t rom_code[16] = {0};
            uc_mem_read(uc, 0x4080a296, rom_code, 16);
            fprintf(stderr, "[60HZ] #%d at 0x%08lx size=%u\n"
                    "  RAM code: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                    "  ROM code: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                    rom_60hz_count, (unsigned long)address, size,
                    code[0],code[1],code[2],code[3],code[4],code[5],code[6],code[7],
                    code[8],code[9],code[10],code[11],code[12],code[13],code[14],code[15],
                    rom_code[0],rom_code[1],rom_code[2],rom_code[3],rom_code[4],rom_code[5],rom_code[6],rom_code[7],
                    rom_code[8],rom_code[9],rom_code[10],rom_code[11],rom_code[12],rom_code[13],rom_code[14],rom_code[15]);
        }
    }

    if (size < cpu->block_stats.min_block_size) {
        cpu->block_stats.min_block_size = size;
    }
    if (size > cpu->block_stats.max_block_size) {
        cpu->block_stats.max_block_size = size;
    }
    cpu->block_stats.sum_block_sizes += size;
    if (size <= 100) {
        cpu->block_stats.block_size_histogram[size]++;
    } else {
        cpu->block_stats.block_size_histogram[100]++;
    }

    /* Trace first blocks after FIX_MEMSIZE */
    if (g_post_fixmem_blocks > 0 && g_post_fixmem_blocks <= 200) {
        uint32_t pc=0, sr=0, a7=0;
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_reg_read(uc, UC_M68K_REG_A7, &a7);
        fprintf(stderr, "[POST-FIXMEM] block#%d addr=0x%08lx size=%u PC=0x%08x A7=0x%08x\n",
                g_post_fixmem_blocks, (unsigned long)address, size, pc, a7);
        g_post_fixmem_blocks++;
    }

    /* Trace ALL TBs near INSTALL_DRIVERS (ROM+0x1142 = 0x02001142) */
    {
        static int install_drv_count = 0;
        /* Trace TBs in the range 0x1100-0x1300, and 0x0370-0x03A0 (called subroutine) */
        if (((address >= 0x02001100 && address <= 0x02001300) ||
             (address >= 0x02001138 && address <= 0x02001160) ||
             (address >= 0x02001250 && address <= 0x02001280) ||
             (address >= 0x020041D0 && address <= 0x02004220)) && install_drv_count < 40) {
            install_drv_count++;
            fprintf(stderr, "[TB-1142] #%d addr=0x%08lx size=%u blocks=%llu\n",
                    install_drv_count, (unsigned long)address, size,
                    (unsigned long long)cpu->block_stats.total_blocks);
        }
        static bool install_drv_traced = false;
        if (!install_drv_traced && address >= 0x02001100 && address <= 0x02001200) {
            install_drv_traced = true;
            uint32_t d0=0, d1=0, a0=0, a7_r=0, sr_r=0;
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            uc_reg_read(uc, UC_M68K_REG_D1, &d1);
            uc_reg_read(uc, UC_M68K_REG_A0, &a0);
            uc_reg_read(uc, UC_M68K_REG_A7, &a7_r);
            uc_reg_read(uc, UC_M68K_REG_SR, &sr_r);
            fprintf(stderr, "[INSTALL-DRV-AREA] PC=0x%08lx blocks=%llu D0=0x%08x D1=0x%08x A0=0x%08x A7=0x%08x SR=0x%04x\n",
                    (unsigned long)address, (unsigned long long)cpu->block_stats.total_blocks,
                    d0, d1, a0, a7_r, sr_r & 0xFFFF);
            /* Dump memory at INSTALL_DRIVERS patch location */
            uint8_t mem[16];
            uc_mem_read(uc, 0x02001140, mem, 16);
            fprintf(stderr, "[INSTALL-DRV-AREA] Bytes at 0x02001140: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                    mem[0],mem[1],mem[2],mem[3],mem[4],mem[5],mem[6],mem[7],
                    mem[8],mem[9],mem[10],mem[11],mem[12],mem[13],mem[14],mem[15]);
        }
    }

    /* Trace when subroutine at 0x41E6 checks $0DD3/$0DD1 */
    {
        static bool dd3_traced = false;
        if (!dd3_traced && address == 0x020041E6) {
            dd3_traced = true;
            uint8_t dd1=0, dd3=0;
            uc_mem_read(uc, 0x0DD1, &dd1, 1);
            uc_mem_read(uc, 0x0DD3, &dd3, 1);
            fprintf(stderr, "[DD3-CHECK] At 0x020041E6: $0DD1=0x%02x (bit0=%d) $0DD3=0x%02x (bit5=%d) blocks=%llu\n",
                    dd1, dd1 & 1, dd3, (dd3 >> 5) & 1,
                    (unsigned long long)cpu->block_stats.total_blocks);
        }
    }

    /* Trace when PC first enters Sony driver area (ROM+0x6c000) */
    {
        static bool sony_traced = false;
        if (!sony_traced && address >= 0x0206c000 && address < 0x0206e000) {
            sony_traced = true;
            uint8_t dd1=0, dd3=0;
            uc_mem_read(uc, 0x0DD1, &dd1, 1);
            uc_mem_read(uc, 0x0DD3, &dd3, 1);
            uint32_t a0=0, a1=0, a7_r=0, sr_r=0;
            uc_reg_read(uc, UC_M68K_REG_A0, &a0);
            uc_reg_read(uc, UC_M68K_REG_A1, &a1);
            uc_reg_read(uc, UC_M68K_REG_A7, &a7_r);
            uc_reg_read(uc, UC_M68K_REG_SR, &sr_r);
            fprintf(stderr, "[SONY-ENTRY] PC=0x%08lx blocks=%llu A0=0x%08x A1=0x%08x A7=0x%08x SR=0x%04x $0DD1=0x%02x $0DD3=0x%02x\n",
                    (unsigned long)address, (unsigned long long)cpu->block_stats.total_blocks,
                    a0, a1, a7_r, sr_r & 0xFFFF, dd1, dd3);
        }
    }

    /* Track last few blocks to find transition to bad PC */
    static uint64_t last_addrs[32] = {0};
    static int last_idx = 0;

    /* Detect code modification at 0x0001CC2E (was 4E75=RTS, crashes when changed to 606A=BRA.S) */
    {
        static uint16_t prev_cc2e = 0;
        static bool cc2e_watch_active = false;
        /* Start watching after block 100M (before the crash at ~139M) */
        if (!cc2e_watch_active && cpu->block_stats.total_blocks > 100000000ULL) {
            uint8_t b[2] = {0};
            uc_mem_read(uc, 0x0001CC2E, b, 2);
            prev_cc2e = (b[0] << 8) | b[1];
            cc2e_watch_active = true;
            fprintf(stderr, "[CODE-WATCH] Started watching 0x0001CC2E, current value: 0x%04x (at block %llu)\n",
                    prev_cc2e, (unsigned long long)cpu->block_stats.total_blocks);
        }
        /* Check every 10000 blocks for changes */
        if (cc2e_watch_active && (cpu->block_stats.total_blocks % 10000) == 0) {
            uint8_t b[2] = {0};
            uc_mem_read(uc, 0x0001CC2E, b, 2);
            uint16_t cur = (b[0] << 8) | b[1];
            if (cur != prev_cc2e) {
                fprintf(stderr, "[CODE-WATCH] *** 0x0001CC2E CHANGED: 0x%04x -> 0x%04x at block %llu ***\n"
                        "  Current PC=0x%08lx\n",
                        prev_cc2e, cur,
                        (unsigned long long)cpu->block_stats.total_blocks,
                        (unsigned long)address);
                /* Dump wider area to see what was written */
                uint8_t area[32] = {0};
                uc_mem_read(uc, 0x0001CC20, area, 32);
                fprintf(stderr, "[CODE-WATCH] Code@0x0001CC20: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                        "                              %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        area[0],area[1],area[2],area[3],area[4],area[5],area[6],area[7],
                        area[8],area[9],area[10],area[11],area[12],area[13],area[14],area[15],
                        area[16],area[17],area[18],area[19],area[20],area[21],area[22],area[23],
                        area[24],area[25],area[26],area[27],area[28],area[29],area[30],area[31]);
                /* Also dump the 32-block trace to see what code was running */
                fprintf(stderr, "[CODE-WATCH] Last 8 blocks: ");
                for (int ri = 8; ri > 0; ri--) {
                    fprintf(stderr, "0x%08lx ", (unsigned long)last_addrs[(last_idx-ri)&31]);
                }
                fprintf(stderr, "\n");
                prev_cc2e = cur;
            }
        }
    }
    {
        last_addrs[last_idx & 31] = address;
        last_idx++;

        /* Stale TB detector: for blocks in the critical patch area (0x0001C000-0x0001D000),
         * check if the JIT might be executing stale code by reading memory and comparing.
         * We can't see the JIT's cached code directly, but we can detect when the
         * hook_block fires at an address where the memory content has changed since
         * the function was first seen. */
        if (address >= 0x0001C000 && address < 0x0001D000 && cpu->block_stats.total_blocks > 100000000ULL) {
            /* Record the first 4 bytes we see at each block address in this range.
             * If a subsequent execution shows different bytes at the same address,
             * the code was modified but the JIT may have a stale TB. */
            static struct { uint32_t addr; uint32_t first_bytes; uint64_t first_block; } seen[128];
            static int seen_count = 0;
            uint8_t mem[4] = {0};
            uc_mem_read(uc, address, mem, 4);
            uint32_t cur_bytes = (mem[0]<<24)|(mem[1]<<16)|(mem[2]<<8)|mem[3];

            int found = -1;
            for (int si = 0; si < seen_count; si++) {
                if (seen[si].addr == (uint32_t)address) {
                    found = si;
                    break;
                }
            }
            if (found >= 0) {
                if (cur_bytes != seen[found].first_bytes) {
                    static int stale_warn_count = 0;
                    if (stale_warn_count++ < 20) {
                        fprintf(stderr, "[STALE-TB] *** Block at 0x%08lx: memory=%08x but was %08x when first seen at blk %llu ***\n"
                                "  Current blk=%llu, JIT may be executing STALE code!\n",
                                (unsigned long)address, cur_bytes, seen[found].first_bytes,
                                (unsigned long long)seen[found].first_block,
                                (unsigned long long)cpu->block_stats.total_blocks);
                        /* Try to fix: flush all TBs */
                        uc_ctl_flush_tb(uc);
                        fprintf(stderr, "[STALE-TB] Flushed TB cache to force re-translation\n");
                        /* Update the record */
                        seen[found].first_bytes = cur_bytes;
                        seen[found].first_block = cpu->block_stats.total_blocks;
                    }
                }
            } else if (seen_count < 128) {
                seen[seen_count].addr = (uint32_t)address;
                seen[seen_count].first_bytes = cur_bytes;
                seen[seen_count].first_block = cpu->block_stats.total_blocks;
                seen_count++;
            }
        }

        if (address < 0x400 && cpu->block_stats.total_blocks > 100) {
            static int bad_pc_count = 0;
            if (bad_pc_count++ < 10) {
                uint32_t pc = 0, sr = 0, a7 = 0, d0 = 0, d1 = 0, d2 = 0;
                uint32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0;
                uc_reg_read(uc, UC_M68K_REG_PC, &pc);
                uc_reg_read(uc, UC_M68K_REG_SR, &sr);
                uc_reg_read(uc, UC_M68K_REG_A7, &a7);
                uc_reg_read(uc, UC_M68K_REG_D0, &d0);
                uc_reg_read(uc, UC_M68K_REG_D1, &d1);
                uc_reg_read(uc, UC_M68K_REG_D2, &d2);
                uc_reg_read(uc, UC_M68K_REG_A0, &a0);
                uc_reg_read(uc, UC_M68K_REG_A1, &a1);
                uc_reg_read(uc, UC_M68K_REG_A2, &a2);
                uc_reg_read(uc, UC_M68K_REG_A3, &a3);
                uc_reg_read(uc, UC_M68K_REG_A4, &a4);
                uc_reg_read(uc, UC_M68K_REG_A5, &a5);
                uc_reg_read(uc, UC_M68K_REG_A6, &a6);
                uint8_t stack[64] = {0};
                uc_mem_read(uc, a7, stack, 64);
                /* Dump code at the crash address */
                uint8_t code_at_addr[16] = {0};
                uc_mem_read(uc, address, code_at_addr, 16);
                /* Dump exception vectors (first 12 vectors, 48 bytes) */
                uint8_t vectors[48] = {0};
                uc_mem_read(uc, 0, vectors, 48);
                /* Dump code at last few block addresses */
                uint8_t prev_code[16] = {0};
                uint64_t prev_addr = last_addrs[(last_idx-2)&31];
                if (prev_addr > 0x400) uc_mem_read(uc, prev_addr, prev_code, 16);
                /* On first BAD PC, dump wider area around crash source */
                if (bad_pc_count == 1) {
                    /* Find all RAM blocks in the ring and dump their code */
                    for (int ri = 1; ri <= 31; ri++) {
                        uint64_t ra = last_addrs[(last_idx-ri)&31];
                        if (ra >= 0x00010000 && ra < 0x01000000) {
                            /* Dump 48 bytes starting from this RAM address */
                            uint8_t ram_code[48] = {0};
                            uc_mem_read(uc, ra, ram_code, 48);
                            fprintf(stderr, "[BAD PC RAM-BLK] @0x%08lx: "
                                    "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x "
                                    "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x "
                                    "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                                    (unsigned long)ra,
                                    ram_code[0],ram_code[1],ram_code[2],ram_code[3],ram_code[4],ram_code[5],ram_code[6],ram_code[7],
                                    ram_code[8],ram_code[9],ram_code[10],ram_code[11],ram_code[12],ram_code[13],ram_code[14],ram_code[15],
                                    ram_code[16],ram_code[17],ram_code[18],ram_code[19],ram_code[20],ram_code[21],ram_code[22],ram_code[23],
                                    ram_code[24],ram_code[25],ram_code[26],ram_code[27],ram_code[28],ram_code[29],ram_code[30],ram_code[31],
                                    ram_code[32],ram_code[33],ram_code[34],ram_code[35],ram_code[36],ram_code[37],ram_code[38],ram_code[39],
                                    ram_code[40],ram_code[41],ram_code[42],ram_code[43],ram_code[44],ram_code[45],ram_code[46],ram_code[47]);
                            /* Check for 0x71xx EmulOps in this code */
                            for (int wi = 0; wi < 46; wi += 2) {
                                uint16_t word = (ram_code[wi] << 8) | ram_code[wi+1];
                                if ((word & 0xFF00) == 0x7100) {
                                    fprintf(stderr, "[BAD PC] *** FOUND 0x71xx at 0x%08lx+%d: 0x%04x ***\n",
                                            (unsigned long)ra, wi, word);
                                }
                            }
                        }
                    }
                    /* Also dump the pre-crash stack (before RTS modified it) */
                    /* A6 = frame pointer, stack before unlk would be around A6 */
                    uint8_t frame_area[32] = {0};
                    uc_mem_read(uc, a6 - 16, frame_area, 32);
                    fprintf(stderr, "[BAD PC FRAME] A6=0x%08x, mem@A6-16:\n  "
                            "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n  "
                            "%02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                            a6,
                            frame_area[0],frame_area[1],frame_area[2],frame_area[3],
                            frame_area[4],frame_area[5],frame_area[6],frame_area[7],
                            frame_area[8],frame_area[9],frame_area[10],frame_area[11],
                            frame_area[12],frame_area[13],frame_area[14],frame_area[15],
                            frame_area[16],frame_area[17],frame_area[18],frame_area[19],
                            frame_area[20],frame_area[21],frame_area[22],frame_area[23],
                            frame_area[24],frame_area[25],frame_area[26],frame_area[27],
                            frame_area[28],frame_area[29],frame_area[30],frame_area[31]);
                }
                fprintf(stderr, "[BAD PC] #%d block#%llu addr=0x%08lx PC=0x%08x SR=0x%04x size=%u\n"
                        "  D: %08x %08x %08x %08x %08x 00000000 00000000 00000000\n"
                        "  A: %08x %08x %08x %08x %08x %08x %08x %08x\n"
                        "  Prev[32]: 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n"
                        "            0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n"
                        "            0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n"
                        "            0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n"
                        "  code@0x%08lx: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                        "  prev@0x%08lx: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                        "  vectors: SSP=%02x%02x%02x%02x PC=%02x%02x%02x%02x BUS=%02x%02x%02x%02x ADDR=%02x%02x%02x%02x\n"
                        "           ILL=%02x%02x%02x%02x DIV0=%02x%02x%02x%02x CHK=%02x%02x%02x%02x TRAPV=%02x%02x%02x%02x\n"
                        "           PRIV=%02x%02x%02x%02x TRACE=%02x%02x%02x%02x LINEA=%02x%02x%02x%02x LINEF=%02x%02x%02x%02x\n"
                        "  stack@0x%08x: %02x%02x %02x%02x %02x%02x %02x%02x | %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        bad_pc_count,
                        (unsigned long long)cpu->block_stats.total_blocks,
                        (unsigned long)address, pc, sr & 0xFFFF, size,
                        d0, d1, d2, 0, 0,
                        a0, a1, a2, a3, a4, a5, a6, a7,
                        (unsigned long)last_addrs[(last_idx-32)&31],
                        (unsigned long)last_addrs[(last_idx-31)&31],
                        (unsigned long)last_addrs[(last_idx-30)&31],
                        (unsigned long)last_addrs[(last_idx-29)&31],
                        (unsigned long)last_addrs[(last_idx-28)&31],
                        (unsigned long)last_addrs[(last_idx-27)&31],
                        (unsigned long)last_addrs[(last_idx-26)&31],
                        (unsigned long)last_addrs[(last_idx-25)&31],
                        (unsigned long)last_addrs[(last_idx-24)&31],
                        (unsigned long)last_addrs[(last_idx-23)&31],
                        (unsigned long)last_addrs[(last_idx-22)&31],
                        (unsigned long)last_addrs[(last_idx-21)&31],
                        (unsigned long)last_addrs[(last_idx-20)&31],
                        (unsigned long)last_addrs[(last_idx-19)&31],
                        (unsigned long)last_addrs[(last_idx-18)&31],
                        (unsigned long)last_addrs[(last_idx-17)&31],
                        (unsigned long)last_addrs[(last_idx-16)&31],
                        (unsigned long)last_addrs[(last_idx-15)&31],
                        (unsigned long)last_addrs[(last_idx-14)&31],
                        (unsigned long)last_addrs[(last_idx-13)&31],
                        (unsigned long)last_addrs[(last_idx-12)&31],
                        (unsigned long)last_addrs[(last_idx-11)&31],
                        (unsigned long)last_addrs[(last_idx-10)&31],
                        (unsigned long)last_addrs[(last_idx-9)&31],
                        (unsigned long)last_addrs[(last_idx-8)&31],
                        (unsigned long)last_addrs[(last_idx-7)&31],
                        (unsigned long)last_addrs[(last_idx-6)&31],
                        (unsigned long)last_addrs[(last_idx-5)&31],
                        (unsigned long)last_addrs[(last_idx-4)&31],
                        (unsigned long)last_addrs[(last_idx-3)&31],
                        (unsigned long)last_addrs[(last_idx-2)&31],
                        (unsigned long)last_addrs[(last_idx-1)&31],
                        (unsigned long)address,
                        code_at_addr[0],code_at_addr[1],code_at_addr[2],code_at_addr[3],
                        code_at_addr[4],code_at_addr[5],code_at_addr[6],code_at_addr[7],
                        code_at_addr[8],code_at_addr[9],code_at_addr[10],code_at_addr[11],
                        code_at_addr[12],code_at_addr[13],code_at_addr[14],code_at_addr[15],
                        (unsigned long)prev_addr,
                        prev_code[0],prev_code[1],prev_code[2],prev_code[3],
                        prev_code[4],prev_code[5],prev_code[6],prev_code[7],
                        prev_code[8],prev_code[9],prev_code[10],prev_code[11],
                        prev_code[12],prev_code[13],prev_code[14],prev_code[15],
                        vectors[0],vectors[1],vectors[2],vectors[3],
                        vectors[4],vectors[5],vectors[6],vectors[7],
                        vectors[8],vectors[9],vectors[10],vectors[11],
                        vectors[12],vectors[13],vectors[14],vectors[15],
                        vectors[16],vectors[17],vectors[18],vectors[19],
                        vectors[20],vectors[21],vectors[22],vectors[23],
                        vectors[24],vectors[25],vectors[26],vectors[27],
                        vectors[28],vectors[29],vectors[30],vectors[31],
                        vectors[32],vectors[33],vectors[34],vectors[35],
                        vectors[36],vectors[37],vectors[38],vectors[39],
                        vectors[40],vectors[41],vectors[42],vectors[43],
                        vectors[44],vectors[45],vectors[46],vectors[47],
                        a7,
                        stack[0],stack[1],stack[2],stack[3],stack[4],stack[5],stack[6],stack[7],
                        stack[8],stack[9],stack[10],stack[11],stack[12],stack[13],stack[14],stack[15]);
                /* Dump trap dispatch circular buffer */
                int num_entries = trap_dispatch_total < TRAP_DISPATCH_BUF_SIZE
                                ? trap_dispatch_total : TRAP_DISPATCH_BUF_SIZE;
                fprintf(stderr, "[BAD PC] Last %d trap dispatches (of %d total):\n",
                        num_entries, trap_dispatch_total);
                for (int di = num_entries; di > 0; di--) {
                    int ei = (trap_dispatch_idx - di) % TRAP_DISPATCH_BUF_SIZE;
                    if (ei < 0) ei += TRAP_DISPATCH_BUF_SIZE;
                    TrapDispatchEntry *te = &trap_dispatch_buf[ei];
                    fprintf(stderr, "  [%s] D1=0x%08x trap#=A0%02x handler=0x%08x A7=0x%08x A6=0x%08x blk=%llu\n",
                            te->is_autopop ? "AUTO" : "NOAP",
                            te->d1, te->d1 & 0xFF, te->handler,
                            te->a7, te->a6,
                            (unsigned long long)te->block_num);
                }
                /* Also dump the OS trap table at crash time */
                fprintf(stderr, "[BAD PC] OS trap table (RAM handlers) at crash:\n");
                uint8_t trap_tbl[1024] = {0};
                uc_mem_read(uc, 0x0400, trap_tbl, 1024);
                for (int ti = 0; ti < 256; ti++) {
                    uint32_t th = (trap_tbl[ti*4]<<24)|(trap_tbl[ti*4+1]<<16)|
                                  (trap_tbl[ti*4+2]<<8)|trap_tbl[ti*4+3];
                    if (th > 0 && th < 0x02000000) {
                        fprintf(stderr, "  A0%02x → 0x%08x%s\n", ti, th,
                                (th >= 0x0001cb00 && th <= 0x0001cd00) ? " *** CRASH RANGE ***" : "");
                    }
                }
            }
        }
    }

    /* Monitor the crashing handler at 0x0001cc16 - dump full state */
    if (address == 0x0001CC16) {
        static int cc16_count = 0;
        cc16_count++;
        uint32_t pc=0, sr=0, a7=0, a6=0, a5=0, d0=0, d1=0, d2=0, d7=0, a4=0;
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_reg_read(uc, UC_M68K_REG_SR, &sr);
        uc_reg_read(uc, UC_M68K_REG_A7, &a7);
        uc_reg_read(uc, UC_M68K_REG_A6, &a6);
        uc_reg_read(uc, UC_M68K_REG_A5, &a5);
        uc_reg_read(uc, UC_M68K_REG_A4, &a4);
        uc_reg_read(uc, UC_M68K_REG_D0, &d0);
        uc_reg_read(uc, UC_M68K_REG_D1, &d1);
        uc_reg_read(uc, UC_M68K_REG_D2, &d2);
        uc_reg_read(uc, UC_M68K_REG_D7, &d7);
        /* Read stack (48 bytes for deeper inspection) */
        uint8_t stk[48] = {0};
        uc_mem_read(uc, a7, stk, 48);
        /* Read code at and around the handler (128 bytes starting from 0x1cbe0) */
        uint8_t code[128] = {0};
        uc_mem_read(uc, 0x0001cbe0, code, 128);
        fprintf(stderr, "[CC16-ENTRY] #%d blk=%llu SR=0x%04x A6=0x%08x A7=0x%08x A5=0x%08x A4=0x%08x\n"
                "  D0=0x%08x D1=0x%08x D2=0x%08x D7=0x%08x\n"
                "  stack@0x%08x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                "                 %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                "                 %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                cc16_count, (unsigned long long)cpu->block_stats.total_blocks,
                sr & 0xFFFF, a6, a7, a5, a4,
                d0, d1, d2, d7,
                a7,
                stk[0],stk[1],stk[2],stk[3],stk[4],stk[5],stk[6],stk[7],
                stk[8],stk[9],stk[10],stk[11],stk[12],stk[13],stk[14],stk[15],
                stk[16],stk[17],stk[18],stk[19],stk[20],stk[21],stk[22],stk[23],
                stk[24],stk[25],stk[26],stk[27],stk[28],stk[29],stk[30],stk[31],
                stk[32],stk[33],stk[34],stk[35],stk[36],stk[37],stk[38],stk[39],
                stk[40],stk[41],stk[42],stk[43],stk[44],stk[45],stk[46],stk[47]);
        /* On first call, dump the handler code */
        if (cc16_count == 1) {
            /* Also read A6-relative frame */
            uint8_t frame[32] = {0};
            if (a6 > 0x100 && a6 < 0x02000000) {
                uc_mem_read(uc, a6 - 16, frame, 32);
                fprintf(stderr, "[CC16-ENTRY] Frame around A6 (0x%08x-16):\n"
                        "  %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                        "  %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        a6,
                        frame[0],frame[1],frame[2],frame[3],frame[4],frame[5],frame[6],frame[7],
                        frame[8],frame[9],frame[10],frame[11],frame[12],frame[13],frame[14],frame[15],
                        frame[16],frame[17],frame[18],frame[19],frame[20],frame[21],frame[22],frame[23],
                        frame[24],frame[25],frame[26],frame[27],frame[28],frame[29],frame[30],frame[31]);
            }
            fprintf(stderr, "[CC16-ENTRY] Handler code (0x0001cbe0-0x0001cc5f):\n");
            for (int ci = 0; ci < 128; ci += 16) {
                fprintf(stderr, "  %06x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        0x0001cbe0 + ci,
                        code[ci+0],code[ci+1],code[ci+2],code[ci+3],
                        code[ci+4],code[ci+5],code[ci+6],code[ci+7],
                        code[ci+8],code[ci+9],code[ci+10],code[ci+11],
                        code[ci+12],code[ci+13],code[ci+14],code[ci+15]);
            }
            /* Also read code at the epilogue area 0x0001cc9a */
            uint8_t epilogue[32] = {0};
            uc_mem_read(uc, 0x0001cc80, epilogue, 32);
            fprintf(stderr, "[CC16-ENTRY] Epilogue code (0x0001cc80-0x0001cc9f):\n"
                    "  %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                    "  %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                    epilogue[0],epilogue[1],epilogue[2],epilogue[3],epilogue[4],epilogue[5],epilogue[6],epilogue[7],
                    epilogue[8],epilogue[9],epilogue[10],epilogue[11],epilogue[12],epilogue[13],epilogue[14],epilogue[15],
                    epilogue[16],epilogue[17],epilogue[18],epilogue[19],epilogue[20],epilogue[21],epilogue[22],epilogue[23],
                    epilogue[24],epilogue[25],epilogue[26],epilogue[27],epilogue[28],epilogue[29],epilogue[30],epilogue[31]);
        }
        /* Read Toolbox trap table to find which entry points here */
        if (cc16_count <= 3) {
            uint8_t tb_base_bytes[4] = {0};
            uc_mem_read(uc, 0x0E7C, tb_base_bytes, 4);
            uint32_t tb_base = (tb_base_bytes[0]<<24)|(tb_base_bytes[1]<<16)|(tb_base_bytes[2]<<8)|tb_base_bytes[3];
            fprintf(stderr, "[CC16-ENTRY] Toolbox table base: 0x%08x\n", tb_base);
            if (tb_base > 0 && tb_base < 0x04000000) {
                /* Scan first 1024 entries for handler pointing to crash range */
                for (int ti = 0; ti < 1024; ti++) {
                    uint8_t entry[4] = {0};
                    uc_mem_read(uc, tb_base + ti * 4, entry, 4);
                    uint32_t handler = (entry[0]<<24)|(entry[1]<<16)|(entry[2]<<8)|entry[3];
                    if (handler >= 0x0001cb00 && handler <= 0x0001cd00) {
                        fprintf(stderr, "[CC16-ENTRY] Toolbox trap A8%03x → 0x%08x\n", ti, handler);
                    }
                }
            }
        }
    }

    /* Monitor function entry at 0x0001cbea - who calls this? */
    if (address == 0x0001CBEA) {
        static int cbea_count = 0;
        cbea_count++;
        uint32_t a7=0, a6=0, a0=0, a1=0, a2=0, d0=0, d1=0, d2=0, sr=0;
        uc_reg_read(uc, UC_M68K_REG_A7, &a7);
        uc_reg_read(uc, UC_M68K_REG_A6, &a6);
        uc_reg_read(uc, UC_M68K_REG_A0, &a0);
        uc_reg_read(uc, UC_M68K_REG_A1, &a1);
        uc_reg_read(uc, UC_M68K_REG_A2, &a2);
        uc_reg_read(uc, UC_M68K_REG_D0, &d0);
        uc_reg_read(uc, UC_M68K_REG_D1, &d1);
        uc_reg_read(uc, UC_M68K_REG_D2, &d2);
        uc_reg_read(uc, UC_M68K_REG_SR, &sr);
        uint8_t stk[64] = {0};
        uc_mem_read(uc, a7, stk, 64);
        /* Read low-memory globals: $03E6 (patch chain?), $0358 (device table) */
        uint8_t lm[16] = {0};
        uc_mem_read(uc, 0x03E6, lm, 4);
        uint32_t val_03e6 = (lm[0]<<24)|(lm[1]<<16)|(lm[2]<<8)|lm[3];
        uc_mem_read(uc, 0x0358, lm, 4);
        uint32_t val_0358 = (lm[0]<<24)|(lm[1]<<16)|(lm[2]<<8)|lm[3];
        fprintf(stderr, "[CBEA-ENTRY] #%d blk=%llu SR=0x%04x A6=0x%08x A7=0x%08x\n"
                "  A0=0x%08x A1=0x%08x A2=0x%08x D0=0x%08x D1=0x%08x D2=0x%08x\n"
                "  $03E6=0x%08x $0358=0x%08x\n"
                "  stack@0x%08x: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n"
                "                 %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n"
                "                 %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n"
                "                 %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                cbea_count, (unsigned long long)cpu->block_stats.total_blocks,
                sr & 0xFFFF, a6, a7,
                a0, a1, a2, d0, d1, d2,
                val_03e6, val_0358,
                a7,
                stk[0],stk[1],stk[2],stk[3],stk[4],stk[5],stk[6],stk[7],
                stk[8],stk[9],stk[10],stk[11],stk[12],stk[13],stk[14],stk[15],
                stk[16],stk[17],stk[18],stk[19],stk[20],stk[21],stk[22],stk[23],
                stk[24],stk[25],stk[26],stk[27],stk[28],stk[29],stk[30],stk[31],
                stk[32],stk[33],stk[34],stk[35],stk[36],stk[37],stk[38],stk[39],
                stk[40],stk[41],stk[42],stk[43],stk[44],stk[45],stk[46],stk[47],
                stk[48],stk[49],stk[50],stk[51],stk[52],stk[53],stk[54],stk[55],
                stk[56],stk[57],stk[58],stk[59],stk[60],stk[61],stk[62],stk[63]);
        /* Dump code at function and key addresses to detect modification */
        uint8_t code_cbea[16] = {0}, code_cc2e[16] = {0}, code_cc16[8] = {0};
        uc_mem_read(uc, 0x0001CBEA, code_cbea, 16);
        uc_mem_read(uc, 0x0001CC2E, code_cc2e, 16);
        uc_mem_read(uc, 0x0001CC16, code_cc16, 8);
        /* Read A004 trap handler address from OS trap table */
        uint8_t a004_bytes[4] = {0};
        uc_mem_read(uc, 0x0410, a004_bytes, 4);  /* 0x0400 + 4*4 */
        uint32_t a004_handler = (a004_bytes[0]<<24)|(a004_bytes[1]<<16)|(a004_bytes[2]<<8)|a004_bytes[3];
        fprintf(stderr, "[CBEA-ENTRY] Code@CBEA: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n"
                "  Code@CC2E: %02x%02x %02x%02x %02x%02x %02x%02x | Code@CC16: %02x%02x %02x%02x\n"
                "  A004 handler: 0x%08x | CBEA expected: 2278 0358=%s CC2E expected: 4e75=%s\n",
                code_cbea[0],code_cbea[1],code_cbea[2],code_cbea[3],
                code_cbea[4],code_cbea[5],code_cbea[6],code_cbea[7],
                code_cbea[8],code_cbea[9],code_cbea[10],code_cbea[11],
                code_cbea[12],code_cbea[13],code_cbea[14],code_cbea[15],
                code_cc2e[0],code_cc2e[1],code_cc2e[2],code_cc2e[3],
                code_cc2e[4],code_cc2e[5],code_cc2e[6],code_cc2e[7],
                code_cc16[0],code_cc16[1],code_cc16[2],code_cc16[3],
                a004_handler,
                (code_cbea[0]==0x22 && code_cbea[1]==0x78 && code_cbea[2]==0x03 && code_cbea[3]==0x58) ? "YES" : "NO!!!",
                (code_cc2e[0]==0x4e && code_cc2e[1]==0x75) ? "YES" : "NO!!!");
        /* On EVERY call, dump the last 32 blocks to trace dispatch path */
        fprintf(stderr, "[CBEA-ENTRY] Last 32 blocks before 0x0001cbea:\n  ");
        for (int ri = 32; ri > 0; ri--) {
            fprintf(stderr, "0x%08lx ", (unsigned long)last_addrs[(last_idx-ri)&31]);
            if (ri % 8 == 1) fprintf(stderr, "\n  ");
        }
        fprintf(stderr, "\n");
    }

    /* Also dump the full dispatcher from 0x020099B0 once */
    if (address == 0x020099B0) {
        static bool full_disp_dumped = false;
        if (!full_disp_dumped) {
            full_disp_dumped = true;
            uint8_t disp[96] = {0};
            uc_mem_read(uc, 0x020099B0, disp, 96);
            fprintf(stderr, "[FULL-DISP] A-line dispatcher (0x020099B0-0x02009A0F):\n");
            for (int ci = 0; ci < 96; ci += 16) {
                fprintf(stderr, "  %06x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        0x020099B0 + ci,
                        disp[ci+0],disp[ci+1],disp[ci+2],disp[ci+3],
                        disp[ci+4],disp[ci+5],disp[ci+6],disp[ci+7],
                        disp[ci+8],disp[ci+9],disp[ci+10],disp[ci+11],
                        disp[ci+12],disp[ci+13],disp[ci+14],disp[ci+15]);
            }
        }
    }

    /* Dump Toolbox dispatcher ROM code once (0x0200efc0-0x0200f200) */
    if (address == 0x0200EFDC) {
        static bool toolbox_dump_done = false;
        if (!toolbox_dump_done) {
            toolbox_dump_done = true;
            uint8_t rom_code[576] = {0};
            uc_mem_read(uc, 0x0200efc0, rom_code, 576);
            fprintf(stderr, "[TB-DISP] Toolbox dispatcher code (0x0200efc0-0x0200f1ff):\n");
            for (int ci = 0; ci < 576; ci += 16) {
                fprintf(stderr, "  %06x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        0x0200efc0 + ci,
                        rom_code[ci+0],rom_code[ci+1],rom_code[ci+2],rom_code[ci+3],
                        rom_code[ci+4],rom_code[ci+5],rom_code[ci+6],rom_code[ci+7],
                        rom_code[ci+8],rom_code[ci+9],rom_code[ci+10],rom_code[ci+11],
                        rom_code[ci+12],rom_code[ci+13],rom_code[ci+14],rom_code[ci+15]);
            }
            /* Also dump the OS trap dispatcher area around 0x020099F0-0x02009A30 */
            uint8_t os_disp[64] = {0};
            uc_mem_read(uc, 0x020099F0, os_disp, 64);
            fprintf(stderr, "[TB-DISP] OS trap dispatcher (0x020099F0-0x02009A2F):\n");
            for (int ci = 0; ci < 64; ci += 16) {
                fprintf(stderr, "  %06x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                        0x020099F0 + ci,
                        os_disp[ci+0],os_disp[ci+1],os_disp[ci+2],os_disp[ci+3],
                        os_disp[ci+4],os_disp[ci+5],os_disp[ci+6],os_disp[ci+7],
                        os_disp[ci+8],os_disp[ci+9],os_disp[ci+10],os_disp[ci+11],
                        os_disp[ci+12],os_disp[ci+13],os_disp[ci+14],os_disp[ci+15]);
            }
        }
    }

    /* Track CMPI/BCS block at 0x020099B0 */
    if (address == 0x020099B0) {
        static int cmpi_block_count = 0;
        if (cmpi_block_count++ < 20) {
            extern uint32_t uc_m68k_get_cc_op(void *uc);
            uint32_t cc_op = uc_m68k_get_cc_op(uc);
            fprintf(stderr, "[CMPI-BLOCK] #%d block@0x020099B0 size=%u: cc_op=%u\n",
                    cmpi_block_count, size, cc_op);
        }
    }

    /* Verify dispatcher instructions at 0x020099FA when OS trap handler block fires */
    if (address == 0x020099F0) {
        static int disp_check_count = 0;
        static uint32_t last_f0_size = 0;
        disp_check_count++;
        uint32_t d2=0;
        extern uint32_t uc_m68k_get_cc_op(void *uc);
        uint32_t cc_op = uc_m68k_get_cc_op(uc);
        uc_reg_read(uc, UC_M68K_REG_D2, &d2);
        /* Always log if size changes or first 30 iterations */
        if (disp_check_count <= 30 || size != last_f0_size) {
            fprintf(stderr, "[DISP-CHECK] #%d block@0x020099F0 size=%u: D2=0x%08x cc_op=%u%s\n",
                    disp_check_count, size, d2, cc_op,
                    (last_f0_size != 0 && size != last_f0_size) ? " *** SIZE CHANGED ***" : "");
        }
        last_f0_size = size;
    }

    /* Check if non-auto-pop path (0x02009A00) is EVER reached */
    if (address == 0x02009A00) {
        static int non_autopop_count = 0;
        non_autopop_count++;
        uint32_t d1=0, d2=0, a0=0, a7=0, a6=0;
        uc_reg_read(uc, UC_M68K_REG_D1, &d1);
        uc_reg_read(uc, UC_M68K_REG_D2, &d2);
        uc_reg_read(uc, UC_M68K_REG_A0, &a0);
        uc_reg_read(uc, UC_M68K_REG_A7, &a7);
        uc_reg_read(uc, UC_M68K_REG_A6, &a6);
        uint8_t trap_num = d1 & 0xFF;
        uint32_t table_addr = 0x0400 + trap_num * 4;
        uint8_t entry[4] = {0};
        uc_mem_read(uc, table_addr, entry, 4);
        uint32_t handler = (entry[0]<<24)|(entry[1]<<16)|(entry[2]<<8)|entry[3];
        /* Always record in circular buffer */
        int bi = trap_dispatch_idx % TRAP_DISPATCH_BUF_SIZE;
        trap_dispatch_buf[bi].d1 = d1;
        trap_dispatch_buf[bi].handler = handler;
        trap_dispatch_buf[bi].a7 = a7;
        trap_dispatch_buf[bi].a6 = a6;
        trap_dispatch_buf[bi].block_num = cpu->block_stats.total_blocks;
        trap_dispatch_buf[bi].is_autopop = 0;
        trap_dispatch_idx++;
        trap_dispatch_total++;
        /* Log first 20 and after FIX_MEMSIZE */
        if (non_autopop_count <= 20 || (g_post_fixmem_blocks > 0 && non_autopop_count <= 500)) {
            fprintf(stderr, "[NON-AUTO-POP] #%d block@0x02009A00: D1=0x%08x D2=0x%08x A0=0x%08x A7=0x%08x A6=0x%08x trap#=0x%02x handler=0x%08x\n",
                    non_autopop_count, d1, d2, a0, a7, a6, trap_num, handler);
        }
    }

    /* Check when auto-pop path is taken */
    if (address == 0x02009A20) {
        static int autopop_count = 0;
        autopop_count++;
        uint32_t d1=0, d2=0, a0=0, a7=0, a6=0;
        uc_reg_read(uc, UC_M68K_REG_D1, &d1);
        uc_reg_read(uc, UC_M68K_REG_D2, &d2);
        uc_reg_read(uc, UC_M68K_REG_A0, &a0);
        uc_reg_read(uc, UC_M68K_REG_A7, &a7);
        uc_reg_read(uc, UC_M68K_REG_A6, &a6);
        uint8_t trap_num = d1 & 0xFF;
        uint32_t table_addr = 0x0400 + trap_num * 4;
        uint8_t entry[4] = {0};
        uc_mem_read(uc, table_addr, entry, 4);
        uint32_t handler = (entry[0]<<24)|(entry[1]<<16)|(entry[2]<<8)|entry[3];
        /* Always record in circular buffer */
        int bi = trap_dispatch_idx % TRAP_DISPATCH_BUF_SIZE;
        trap_dispatch_buf[bi].d1 = d1;
        trap_dispatch_buf[bi].handler = handler;
        trap_dispatch_buf[bi].a7 = a7;
        trap_dispatch_buf[bi].a6 = a6;
        trap_dispatch_buf[bi].block_num = cpu->block_stats.total_blocks;
        trap_dispatch_buf[bi].is_autopop = 1;
        trap_dispatch_idx++;
        trap_dispatch_total++;
        if (autopop_count <= 5 || (g_post_fixmem_blocks > 0 && autopop_count <= 500)) {
            fprintf(stderr, "[AUTO-POP] #%d D1=0x%08x D2=0x%08x A7=0x%08x A6=0x%08x trap#=0x%02x handler=0x%08x\n",
                    autopop_count, d1, d2, a7, a6, trap_num, handler);
        }
    }

    /* Check the BNE block itself at 0x020099FE */
    if (address == 0x020099FE) {
        static int bne_check_count = 0;
        if (bne_check_count++ < 10) {
            uint32_t d0=0, d1=0, d2=0, sr=0, a0=0, a2=0, a7=0;
            extern uint32_t uc_m68k_get_cc_op(void *uc);
            uint32_t cc_op = uc_m68k_get_cc_op(uc);
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            uc_reg_read(uc, UC_M68K_REG_D1, &d1);
            uc_reg_read(uc, UC_M68K_REG_D2, &d2);
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);
            uc_reg_read(uc, UC_M68K_REG_A0, &a0);
            uc_reg_read(uc, UC_M68K_REG_A2, &a2);
            uc_reg_read(uc, UC_M68K_REG_A7, &a7);
            /* Read stack to see exception frame and saved registers */
            uint8_t stack[32] = {0};
            uc_mem_read(uc, a7, stack, 32);
            fprintf(stderr, "[BNE-BLOCK] #%d block@0x020099FE size=%u: D0=0x%08x D1=0x%08x D2=0x%08x SR=0x%04x cc_op=%u\n"
                    "  A0=0x%08x A2=0x%08x A7=0x%08x\n"
                    "  Previous 8 blocks: 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx 0x%08lx\n"
                    "  stack: %02x%02x %02x%02x%02x%02x %02x%02x | %02x%02x %02x%02x%02x%02x %02x%02x\n",
                    bne_check_count, size, d0, d1, d2, sr & 0xFFFF, cc_op,
                    a0, a2, a7,
                    (unsigned long)last_addrs[(last_idx-8)&31],
                    (unsigned long)last_addrs[(last_idx-7)&31],
                    (unsigned long)last_addrs[(last_idx-6)&31],
                    (unsigned long)last_addrs[(last_idx-5)&31],
                    (unsigned long)last_addrs[(last_idx-4)&31],
                    (unsigned long)last_addrs[(last_idx-3)&31],
                    (unsigned long)last_addrs[(last_idx-2)&31],
                    (unsigned long)last_addrs[(last_idx-1)&31],
                    stack[0],stack[1],stack[2],stack[3],stack[4],stack[5],stack[6],stack[7],
                    stack[8],stack[9],stack[10],stack[11],stack[12],stack[13],stack[14],stack[15]);
        }
    }

    /* Check D0 at the 60Hz handler TST.L D0 (0x0200A29C) */
    if (address == 0x0200A29C) {
        static int tst_d0_count = 0;
        if (tst_d0_count++ < 30) {
            uint32_t d0=0;
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            fprintf(stderr, "[TST-D0] #%d at 0x0200A29C: D0=0x%08x\n", tst_d0_count, d0);
        }
    }

    /* Check BSET #6,$0160 and its result */
    if (address == 0x0200A2A0) {
        static int bset_count = 0;
        bset_count++;
        if (bset_count <= 30) {
            uint32_t d0=0, a7=0, sr=0;
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            uc_reg_read(uc, UC_M68K_REG_A7, &a7);
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);
            uint8_t val_0160 = 0;
            uc_mem_read(uc, 0x0160, &val_0160, 1);
            fprintf(stderr, "[BSET-160] #%d at 0x0200A2A0 (blocks=%llu): $0160=0x%02x (bit6=%d) D0=0x%08x SR=0x%04x A7=0x%08x\n",
                    bset_count, (unsigned long long)cpu->block_stats.total_blocks,
                    val_0160, (val_0160 >> 6) & 1, d0, sr & 0xFFFF, a7);
        }
    }

    /* Track SCSI probe entry/exit to find D5 corruption */
    if (address == 0x020071f0) {
        /* SCSI probe entry */
        static int probe_log = 0;
        if (++probe_log <= 5) {
            uint32_t d5 = 0;
            uc_reg_read(uc, UC_M68K_REG_D5, &d5);
            fprintf(stderr, "[SCSI-PROBE] entry #%d: D5=%u\n", probe_log, d5);
        }
    }
    if (address == 0x02007222) {
        /* SCSI probe exit (rts) */
        static int probe_exit = 0;
        if (++probe_exit <= 5) {
            uint32_t d5 = 0, d0 = 0;
            uc_reg_read(uc, UC_M68K_REG_D5, &d5);
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            fprintf(stderr, "[SCSI-PROBE] exit  #%d: D5=%u D0=0x%08x\n", probe_exit, d5, d0);
        }
    }

    /* Trace BCLR #6,$0160 at 0x0200A1EE - the bit6 clear */
    if (address == 0x0200A1EE) {
        static int bclr_count = 0;
        if (bclr_count++ < 20) {
            uint8_t val_0160 = 0;
            uc_mem_read(uc, 0x0160, &val_0160, 1);
            fprintf(stderr, "[BCLR-160] #%d at 0x0200A1EE (blocks=%llu): $0160=0x%02x (bit6=%d)\n",
                    bclr_count, (unsigned long long)cpu->block_stats.total_blocks,
                    val_0160, (val_0160 >> 6) & 1);
        }
    }

    /* Trace MOVE #$2000,SR at 0x0200A2A8 - code after successful BSET */
    if (address == 0x0200A2A8) {
        static int move_sr_count = 0;
        if (move_sr_count++ < 10) {
            uint32_t sr=0, a7=0;
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);
            uc_reg_read(uc, UC_M68K_REG_A7, &a7);
            fprintf(stderr, "[MOVE-SR] #%d at 0x0200A2A8 (blocks=%llu): SR=0x%04x A7=0x%08x\n",
                    move_sr_count, (unsigned long long)cpu->block_stats.total_blocks,
                    sr & 0xFFFF, a7);
        }
    }

    /* SCSI scan: cap D5 timeout to prevent 545-second wait from register corruption.
     * The SCSI probe at 0x71F0 corrupts D5 from its original value (240 = 4 seconds)
     * to 32766 on every call. We restore it to the original deadline. */
    if (address == 0x020014be) {
        /* This is the timeout check: cmp.l (0x016A).w, D5 */
        static uint32_t original_d5 = 0;
        uint32_t d5 = 0;
        uc_reg_read(uc, UC_M68K_REG_D5, &d5);
        if (original_d5 == 0 && d5 <= 1200) {
            /* Capture the first reasonable D5 as the original deadline */
            original_d5 = d5;
        }
        if (d5 > 1200) {  /* Corrupted - restore to original absolute deadline */
            static int cap_count = 0;
            if (++cap_count <= 3) {
                fprintf(stderr, "[SCSI-CAP] D5 corrupted to %u, restoring to %u\n", d5, original_d5 ? original_d5 : 240);
            }
            d5 = original_d5 ? original_d5 : 240;
            uc_reg_write(uc, UC_M68K_REG_D5, &d5);
        }
    }
    /* Debug Ticks delay loop at 0x020014ca */
    if (address == 0x020014ca) {
        static uint64_t ticks_loop_count = 0;
        ticks_loop_count++;
        /* Log the first entry after each delay loop exit (D0 changes indicate re-entry) */
        static uint32_t last_d0 = 0xFFFFFFFF;
        uint32_t d0 = 0;
        uc_reg_read(uc, UC_M68K_REG_D0, &d0);
        if (d0 != last_d0) {
            /* D0 changed - this is a new delay loop entry */
            static int delay_entry_count = 0;
            if (++delay_entry_count <= 10) {
                uint8_t tb[4] = {0};
                uc_mem_read(uc, 0x16a, tb, 4);
                uint32_t ticks_uc = (tb[0]<<24)|(tb[1]<<16)|(tb[2]<<8)|tb[3];
                uint32_t d5 = 0;
                uc_reg_read(uc, UC_M68K_REG_D5, &d5);
                fprintf(stderr, "[SCSI-SCAN] #%d D5(deadline)=%u Ticks=%u D0=0x%08x\n",
                        delay_entry_count, d5, ticks_uc, d0);
            }
            last_d0 = d0;
        }
    }
    /* SCSI scan timeout exit at ROM 0x14D2 */
    if (address == 0x020014d2) {
        uint8_t tb[4] = {0};
        uc_mem_read(uc, 0x16a, tb, 4);
        uint32_t ticks_uc = (tb[0]<<24)|(tb[1]<<16)|(tb[2]<<8)|tb[3];
        uint32_t d5 = 0;
        uc_reg_read(uc, UC_M68K_REG_D5, &d5);
        fprintf(stderr, "*** [SCSI-TIMEOUT] SCSI scan timed out! D5=%u Ticks=%u (boot continuing) ***\n", d5, ticks_uc);
    }
    /* SCSI scan success exit at ROM 0x14D8 */
    if (address == 0x020014d8) {
        fprintf(stderr, "*** [SCSI-SUCCESS] SCSI scan found device! (boot continuing) ***\n");
    }
    /* SCSI scan return at ROM 0x14DE (both paths reach here) */
    if (address == 0x020014de) {
        fprintf(stderr, "*** [SCSI-DONE] SCSI scan routine returning (blocks=%llu) ***\n",
                (unsigned long long)cpu->block_stats.total_blocks);
    }

    /* ioResult polling loop at ROM 0xbb8c: move.w $0010(A0), D0 / bne.s $bb8c */
    if (address == 0x0200bb8c) {
        static int io_poll_count = 0;
        io_poll_count++;
        if (io_poll_count == 1) {
            /* One-shot full state dump on first entry */
            uint32_t regs[16] = {0};  /* D0-D7, A0-A7 */
            uint32_t sr = 0;
            for (int i = 0; i < 8; i++) {
                uc_reg_read(uc, UC_M68K_REG_D0 + i, &regs[i]);
                uc_reg_read(uc, UC_M68K_REG_A0 + i, &regs[8+i]);
            }
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);

            fprintf(stderr, "*** [IO-POLL-ENTRY] First entry to ioResult polling loop ***\n");
            fprintf(stderr, "  D0=%08x D1=%08x D2=%08x D3=%08x D4=%08x D5=%08x D6=%08x D7=%08x\n",
                    regs[0],regs[1],regs[2],regs[3],regs[4],regs[5],regs[6],regs[7]);
            fprintf(stderr, "  A0=%08x A1=%08x A2=%08x A3=%08x A4=%08x A5=%08x A6=%08x SP=%08x SR=%04x\n",
                    regs[8],regs[9],regs[10],regs[11],regs[12],regs[13],regs[14],regs[15],sr&0xFFFF);

            /* Read stack (return address and parameters) */
            uint8_t stack[32] = {0};
            uc_mem_read(uc, regs[15], stack, 32);
            fprintf(stderr, "  Stack@SP: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                    stack[0],stack[1],stack[2],stack[3],stack[4],stack[5],stack[6],stack[7],
                    stack[8],stack[9],stack[10],stack[11],stack[12],stack[13],stack[14],stack[15]);

            /* Read the parameter block (first 48 bytes at A0) - works for any address */
            uint32_t a0 = regs[8];
            uint8_t pb[48] = {0};
            uc_err pb_err = uc_mem_read(uc, a0, pb, 48);
            int16_t ioResult = (int16_t)((pb[16]<<8)|pb[17]);
            int16_t ioRefNum = (int16_t)((pb[24]<<8)|pb[25]);
            uint16_t ioTrap = (pb[6]<<8)|pb[7];
            uint32_t ioComp = (pb[12]<<24)|(pb[13]<<16)|(pb[14]<<8)|pb[15];
            fprintf(stderr, "  PB@0x%08x (uc_err=%d): ioTrap=0x%04x ioResult=%d ioRefNum=%d ioCompletion=0x%08x\n",
                    a0, pb_err, ioTrap, ioResult, ioRefNum, ioComp);
            fprintf(stderr, "  PB raw: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n"
                    "          %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                    pb[0],pb[1],pb[2],pb[3],pb[4],pb[5],pb[6],pb[7],pb[8],pb[9],pb[10],pb[11],pb[12],pb[13],pb[14],pb[15],
                    pb[16],pb[17],pb[18],pb[19],pb[20],pb[21],pb[22],pb[23],pb[24],pb[25],pb[26],pb[27],pb[28],pb[29],pb[30],pb[31]);
            /* Also check if A0 is in valid RAM range */
            if (a0 >= 0x02000000) {
                fprintf(stderr, "  WARNING: PB at 0x%08x is OUTSIDE RAM (0-0x01FFFFFF)!\n", a0);
            }
        }
    }

    /* Log hot loop PC after settling (diagnostic) */
    if (cpu->block_stats.total_blocks == 500000ULL) {
        uint32_t pc = 0, sr = 0, a7 = 0, d0 = 0, d1 = 0;
        uint8_t code[16] = {0};
        uint8_t vec[16] = {0};
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_reg_read(uc, UC_M68K_REG_SR, &sr);
        uc_reg_read(uc, UC_M68K_REG_A7, &a7);
        uc_reg_read(uc, UC_M68K_REG_D0, &d0);
        uc_reg_read(uc, UC_M68K_REG_D1, &d1);
        /* Read code at block address AND at word-aligned address */
        uc_mem_read(uc, address & ~1ULL, code, 16);
        /* Read vector table entries */
        uc_mem_read(uc, 0x00, vec, 16);
        fprintf(stderr, "[HOT LOOP] 10M blocks: hook_addr=0x%08lx reg_PC=0x%08x size=%u SR=0x%04x A7=0x%08x D0=0x%08x D1=0x%08x\n",
                (unsigned long)address, pc, size, sr & 0xFFFF, a7, d0, d1);
        fprintf(stderr, "  code@0x%08lx: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x\n",
                (unsigned long)(address & ~1ULL),
                code[0],code[1],code[2],code[3],code[4],code[5],code[6],code[7],
                code[8],code[9],code[10],code[11],code[12],code[13],code[14],code[15]);
        fprintf(stderr, "  vectors: SSP=%02x%02x%02x%02x PC=%02x%02x%02x%02x BusErr=%02x%02x%02x%02x AddrErr=%02x%02x%02x%02x\n",
                vec[0],vec[1],vec[2],vec[3], vec[4],vec[5],vec[6],vec[7],
                vec[8],vec[9],vec[10],vec[11], vec[12],vec[13],vec[14],vec[15]);
    }

    /* Poll timer every 4096 blocks (not every block!)
     * At ~60Hz timer rate, even polling 100x/second is more than enough.
     * With natural TB sizes (~10-50 instructions), 4096 blocks ≈ 40K-200K instructions.
     * At 100M instructions/second, that's ~500-2500 polls/second - plenty for 60Hz. */
    extern uint64_t poll_timer_interrupt(void);
    if ((cpu->block_stats.total_blocks & 0xFFF) == 0) {
        uint64_t fired = poll_timer_interrupt();

        /* Update diagnostic counters */
        diag_timer_polls++;
        if (fired) {
            diag_timer_fires++;
            /* TB flush removed: QEMU's notdirty_write() path handles SMC.
             * Guest M68K stores go through store_helper() -> notdirty_write() ->
             * tb_invalidate_phys_page_fast(), which invalidates TBs for pages
             * with UC_PROT_EXEC. Since RAM is mapped with UC_PROT_ALL, Mac OS
             * heap overwrites of EmulOp patches are caught automatically.
             * See docs/deepdive/JIT_SMC_Detection_Analysis.md for full analysis. */
        }

        if ((cpu->block_stats.total_blocks & 0x1FFFFF) == 0) {
            uint32_t pc = 0, sr = 0;
            uc_reg_read(uc, UC_M68K_REG_PC, &pc);
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);
            int ipl = ((sr & 0xFFFF) >> 8) & 7;
            extern volatile int g_pending_interrupt_level;
            /* Get wall-clock time for rate analysis */
            static uint64_t diag_start_ns = 0;
            struct timespec diag_ts;
            clock_gettime(CLOCK_MONOTONIC, &diag_ts);
            uint64_t diag_now = diag_ts.tv_sec * 1000000000ULL + diag_ts.tv_nsec;
            if (diag_start_ns == 0) diag_start_ns = diag_now;
            uint64_t elapsed_ms = (diag_now - diag_start_ns) / 1000000;
            /* Read Ticks variable at 0x16a for Ticks loop debugging */
            uint8_t ticks_bytes[4] = {0};
            uc_err ticks_err = uc_mem_read(uc, 0x16a, ticks_bytes, 4);
            uint32_t ticks_val = (ticks_bytes[0]<<24)|(ticks_bytes[1]<<16)|(ticks_bytes[2]<<8)|ticks_bytes[3];
            /* Also read directly from host memory for comparison */
            extern uint8_t *RAMBaseHost;
            extern uint32_t RAMSize;
            uint32_t host_ticks = 0;
            if (RAMBaseHost && RAMSize > 0x16e) {
                uint8_t *p = RAMBaseHost + 0x16a;
                host_ticks = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
            }
            /* Read low memory global at 0x0b78 to track boot stall cause */
            static uint32_t prev_0b78 = 0xDEADBEEF;
            uint32_t val_0b78 = 0;
            if (RAMBaseHost && RAMSize > 0x0b7c) {
                uint8_t *p = RAMBaseHost + 0x0b78;
                val_0b78 = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
            }
            if (val_0b78 != prev_0b78) {
                fprintf(stderr, "[DIAG] *** $0b78 CHANGED: 0x%08x -> 0x%08x at blocks=%lluM PC=0x%08x\n",
                        prev_0b78, val_0b78,
                        (unsigned long long)(cpu->block_stats.total_blocks >> 20), pc);
                prev_0b78 = val_0b78;
            }
            /* Track OS trap table evolution: count entries pointing to RAM */
            static int prev_trap_count = -1;
            int trap_count = 0;
            if (RAMBaseHost && RAMSize > 0x0800) {
                for (int i = 0; i < 256; i++) {
                    uint8_t *p = RAMBaseHost + 0x0400 + i * 4;
                    uint32_t handler = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
                    if (handler > 0 && handler < 0x02000000) {
                        trap_count++;
                    }
                }
            }
            if (trap_count != prev_trap_count) {
                fprintf(stderr, "[DIAG] *** TRAP TABLE CHANGED: %d -> %d RAM entries at blocks=%lluM PC=0x%08x\n",
                        prev_trap_count, trap_count,
                        (unsigned long long)(cpu->block_stats.total_blocks >> 20), pc);
                /* Dump all current entries when count changes */
                if (RAMBaseHost && RAMSize > 0x0800) {
                    for (int i = 0; i < 256; i++) {
                        uint8_t *p = RAMBaseHost + 0x0400 + i * 4;
                        uint32_t handler = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
                        if (handler > 0 && handler < 0x02000000) {
                            fprintf(stderr, "[DIAG]   A0%02x -> 0x%08x\n", i, handler);
                        }
                    }
                }
                prev_trap_count = trap_count;
            }

            /* Watch address 0x01FFF30C - resource chain sentinel */
            static uint32_t prev_1fff30c = 0xDEAD0000;
            uint32_t val_1fff30c = 0;
            if (RAMBaseHost && RAMSize > 0x01FFF310) {
                uint8_t *p = RAMBaseHost + 0x01FFF30C;
                val_1fff30c = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
            }
            if (val_1fff30c != prev_1fff30c) {
                /* Also read resource manager globals */
                uint32_t topmap=0, sysmap=0, curmap=0, syszone=0;
                if (RAMBaseHost && RAMSize > 0x0A60) {
                    uint8_t *p;
                    p = RAMBaseHost + 0x0A50; topmap = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
                    p = RAMBaseHost + 0x0A54; sysmap = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
                    p = RAMBaseHost + 0x0A5A; curmap = (p[0]<<8)|p[1];
                    p = RAMBaseHost + 0x02A6; syszone = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
                }
                fprintf(stderr, "[DIAG] *** [0x01FFF30C] CHANGED: 0x%08x -> 0x%08x at blocks=%lluM PC=0x%08x\n"
                        "  TopMapHndl=$A50=0x%08x SysMapHndl=$A54=0x%08x CurMap=$A5A=0x%04x SysZone=$2A6=0x%08x\n",
                        prev_1fff30c, val_1fff30c,
                        (unsigned long long)(cpu->block_stats.total_blocks >> 20), pc,
                        topmap, sysmap, curmap, syszone);
                /* Dump 64 bytes around the address */
                if (RAMBaseHost && RAMSize > 0x01FFF340) {
                    uint8_t *p = RAMBaseHost + 0x01FFF2F0;
                    fprintf(stderr, "  Mem@01FFF2F0:");
                    for (int j = 0; j < 64; j += 4)
                        fprintf(stderr, " %02x%02x%02x%02x", p[j], p[j+1], p[j+2], p[j+3]);
                    fprintf(stderr, "\n");
                }
                prev_1fff30c = val_1fff30c;
            }

            fprintf(stderr, "[DIAG] blocks=%lluM t=%llums polls=%llu fires=%llu delivered=%llu blocked=%llu "
                    "PC=0x%08x SR=0x%04x IPL=%d pending=%d Ticks=%u host_Ticks=%u $0b78=0x%08x traps=%d uc_err=%d\n",
                    (unsigned long long)(cpu->block_stats.total_blocks >> 20),
                    (unsigned long long)elapsed_ms,
                    (unsigned long long)diag_timer_polls,
                    (unsigned long long)diag_timer_fires,
                    (unsigned long long)diag_irq_delivered,
                    (unsigned long long)diag_irq_blocked,
                    pc, sr & 0xFFFF, ipl, g_pending_interrupt_level,
                    ticks_val, host_ticks, val_0b78, trap_count, ticks_err);

            /* Stall detection: dump code + registers when PC stays the same */
            static uint32_t stall_pc = 0;
            static int stall_count = 0;
            if (pc == stall_pc) {
                stall_count++;
                if (stall_count == 5) {
                    /* PC has been the same for 5 consecutive 2M-block samples = stall */
                    /* Dump code from PC-32 to PC+64 to capture loop heads */
                    uint32_t dump_start = (pc >= 32) ? pc - 32 : 0;
                    uint8_t code[96] = {0};
                    uc_mem_read(uc, dump_start, code, 96);
                    fprintf(stderr, "[STALL] PC=0x%08x stuck for 10M+ blocks. Code dump (from PC-32):\n", pc);
                    for (int row = 0; row < 96; row += 16) {
                        fprintf(stderr, "[STALL] %08x:", dump_start + row);
                        for (int j = 0; j < 16; j += 2)
                            fprintf(stderr, " %02x%02x", code[row+j], code[row+j+1]);
                        fprintf(stderr, "%s\n", (dump_start + row == (pc & ~0xF)) ? " <-- stall" : "");
                    }
                    /* Dump registers */
                    uint32_t dregs[8], aregs[8];
                    for (int j = 0; j < 8; j++) {
                        uc_reg_read(uc, UC_M68K_REG_D0 + j, &dregs[j]);
                        uc_reg_read(uc, UC_M68K_REG_A0 + j, &aregs[j]);
                    }
                    fprintf(stderr, "[STALL] D: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                            dregs[0], dregs[1], dregs[2], dregs[3], dregs[4], dregs[5], dregs[6], dregs[7]);
                    fprintf(stderr, "[STALL] A: %08x %08x %08x %08x %08x %08x %08x %08x\n",
                            aregs[0], aregs[1], aregs[2], aregs[3], aregs[4], aregs[5], aregs[6], aregs[7]);
                    /* Dump memory at A4 (linked list entry) */
                    if (aregs[4] != 0) {
                        uint8_t a4mem[64] = {0};
                        uc_mem_read(uc, aregs[4], a4mem, 64);
                        fprintf(stderr, "[STALL] Mem@A4(0x%08x):", aregs[4]);
                        for (int j = 0; j < 64; j += 4)
                            fprintf(stderr, " %02x%02x%02x%02x", a4mem[j], a4mem[j+1], a4mem[j+2], a4mem[j+3]);
                        fprintf(stderr, "\n");
                        /* Decode linked list fields */
                        uint32_t next_ptr = (a4mem[0]<<24)|(a4mem[1]<<16)|(a4mem[2]<<8)|a4mem[3];
                        uint32_t field_1c = 0, field_28 = 0;
                        uint8_t field_15 = 0;
                        if (64 > 0x1f) {
                            field_1c = (a4mem[0x1c]<<24)|(a4mem[0x1d]<<16)|(a4mem[0x1e]<<8)|a4mem[0x1f];
                        }
                        if (64 > 0x2b) {
                            field_28 = (a4mem[0x28]<<24)|(a4mem[0x29]<<16)|(a4mem[0x2a]<<8)|a4mem[0x2b];
                        }
                        if (64 > 0x15) {
                            field_15 = a4mem[0x15];
                        }
                        fprintf(stderr, "[STALL] A4 struct: [A4+0]=%08x [A4+0x15]=%02x [A4+0x1c]=%08x [A4+0x28]=%08x\n",
                                next_ptr, field_15, field_1c, field_28);
                        fprintf(stderr, "[STALL] Comparing: D3=%08x vs [A4+0x28]=%08x, A2=%08x vs [A4+0x1c]=%08x, D6.B=%02x vs [A4+0x15]=%02x\n",
                                dregs[3], field_28, aregs[2], field_1c, dregs[6] & 0xFF, field_15);
                    }
                    /* Dump stack for return address chain */
                    uint8_t stack[64] = {0};
                    uc_mem_read(uc, aregs[7], stack, 64);
                    fprintf(stderr, "[STALL] Stack @0x%08x:\n", aregs[7]);
                    for (int j = 0; j < 64; j += 16) {
                        fprintf(stderr, "[STALL]   +%02x:", j);
                        for (int k = 0; k < 16; k += 4)
                            fprintf(stderr, " %02x%02x%02x%02x", stack[j+k], stack[j+k+1], stack[j+k+2], stack[j+k+3]);
                        fprintf(stderr, "\n");
                    }
                    /* Walk the linked list from A3 (sentinel) to find corrupt entry */
                    if (aregs[3] != 0 && aregs[3] < 0x02000000) {
                        fprintf(stderr, "[STALL] Walking linked list from A3=0x%08x:\n", aregs[3]);
                        uint32_t walk = aregs[3];
                        for (int step = 0; step < 200; step++) {
                            uint8_t next_bytes[4] = {0};
                            uc_mem_read(uc, walk, next_bytes, 4);
                            uint32_t next = (next_bytes[0]<<24)|(next_bytes[1]<<16)|(next_bytes[2]<<8)|next_bytes[3];
                            /* Also read fields used in comparison */
                            uint8_t f15 = 0;
                            uint32_t f1c = 0, f28 = 0;
                            if (next != aregs[3]) {  /* Don't read fields of sentinel */
                                uint8_t fb[4] = {0};
                                uc_mem_read(uc, next + 0x15, &f15, 1);
                                uc_mem_read(uc, next + 0x1c, fb, 4);
                                f1c = (fb[0]<<24)|(fb[1]<<16)|(fb[2]<<8)|fb[3];
                                uc_mem_read(uc, next + 0x28, fb, 4);
                                f28 = (fb[0]<<24)|(fb[1]<<16)|(fb[2]<<8)|fb[3];
                            }
                            int bad = (next != aregs[3] && (next == 0 || next >= 0x02000000));
                            fprintf(stderr, "[STALL]   [%d] @0x%08x -> 0x%08x%s",
                                    step, walk, next, bad ? " *** BAD ***" : "");
                            if (next != aregs[3]) {
                                fprintf(stderr, " [+15]=%02x [+1c]=%08x [+28]=%08x", f15, f1c, f28);
                            }
                            fprintf(stderr, "\n");
                            if (next == aregs[3]) {
                                fprintf(stderr, "[STALL]   Chain complete: %d entries (back to sentinel)\n", step);
                                break;
                            }
                            if (next == 0 || next >= 0x04000000) {
                                fprintf(stderr, "[STALL]   Chain BROKEN at step %d: ptr=0x%08x\n", step, next);
                                break;
                            }
                            walk = next;
                        }
                    }
                    /* Check Mac OS boot state globals */
                    uint8_t mac_globals[16] = {0};
                    uc_mem_read(uc, 0x012f, mac_globals, 1);    /* WarmStart */
                    uc_mem_read(uc, 0x0910, mac_globals+4, 4);  /* CurApName (first 4 bytes) */
                    uc_mem_read(uc, 0x0904, mac_globals+8, 4);  /* CurrentA5 */
                    uc_mem_read(uc, 0x0908, mac_globals+12, 4); /* CurStackBase */
                    fprintf(stderr, "[STALL] Mac globals: WarmStart=0x%02x CurApName='%.4s' CurrentA5=0x%08x CurStackBase=0x%08x\n",
                            mac_globals[0],
                            mac_globals+4,
                            (mac_globals[8]<<24)|(mac_globals[9]<<16)|(mac_globals[10]<<8)|mac_globals[11],
                            (mac_globals[12]<<24)|(mac_globals[13]<<16)|(mac_globals[14]<<8)|mac_globals[15]);
                }
            } else {
                stall_pc = pc;
                stall_count = 0;
            }
        }
    }

    /* Apply any deferred register updates at block boundaries.
     * This is needed because EmulOp handlers defer register changes
     * that must be applied before the next instruction executes. */
    apply_deferred_updates_and_flush(cpu, uc, "hook_block");

    /* PC-based tracing (cached env lookup) */
    if (!cpu->pc_trace_enabled) {
        static const char *trace_pc_env = NULL;
        static bool env_checked = false;
        if (!env_checked) {
            trace_pc_env = getenv("CPU_TRACE_PC");
            env_checked = true;
        }
        if (trace_pc_env) {
            uint32_t trace_pc = strtoul(trace_pc_env, NULL, 0);
            if (address == trace_pc) {
                fprintf(stderr, "[Unicorn] PC-based trace triggered at 0x%08lx\n", address);
                cpu->pc_trace_enabled = true;
                extern void cpu_trace_force_enable(void);
                cpu_trace_force_enable();
            }
        }
    }

    /* Check for pending interrupts (cheap - just reads a global) */
    if (g_pending_interrupt_level > 0) {
        uint32_t sr = 0;
        uc_reg_read(uc, UC_M68K_REG_SR, &sr);
        int current_ipl = ((sr & 0xFFFF) >> 8) & 7;

        if (g_pending_interrupt_level > current_ipl) {
            uint8_t vector = (g_pending_interrupt_level == 1) ? 0x19 :
                           (0x18 + g_pending_interrupt_level);

            static int irq_deliver_count = 0;
            diag_irq_delivered++;
            if (++irq_deliver_count <= 50) {
                fprintf(stderr, "[IRQ-DELIVER] #%d blocks=%llu addr=0x%08lx SR=0x%04x IPL=%d level=%d\n",
                        irq_deliver_count, (unsigned long long)cpu->block_stats.total_blocks,
                        (unsigned long)address, sr & 0xFFFF, current_ipl, g_pending_interrupt_level);
            }

            uc_m68k_trigger_interrupt(uc, g_pending_interrupt_level, vector);
            g_pending_interrupt_level = 0;

            /* Schedule deferred clear of QEMU's pending_level. We can't
             * clear it now because QEMU hasn't delivered the interrupt yet
             * (that happens on the next uc_emu_start). The first hook_block
             * after delivery will clear it - at that point the handler is
             * running with IPL raised, mimicking hardware interrupt ack. */
            g_interrupt_pending_ack = true;

            uc_emu_stop(uc);
        } else {
            /* IRQ blocked by IPL */
            static int irq_blocked_count = 0;
            diag_irq_blocked++;
            if (++irq_blocked_count <= 20) {
                fprintf(stderr, "[IRQ-BLOCKED] #%d blocks=%llu addr=0x%08lx SR=0x%04x IPL=%d pending=%d\n",
                        irq_blocked_count, (unsigned long long)cpu->block_stats.total_blocks,
                        (unsigned long)address, sr & 0xFFFF, current_ipl, g_pending_interrupt_level);
            }
        }
    }
}

/**
 * Hook for CPU exceptions (UC_HOOK_INTR)
 * Handles A-line exceptions for EmulOps (0xAE00-0xAE3F)
 */
static void hook_interrupt(uc_engine *uc, uint32_t intno, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    /* Trace hook_interrupt near INSTALL_DRIVERS */
    {
        uint32_t dbg_pc;
        uc_reg_read(uc, UC_M68K_REG_PC, &dbg_pc);
        if (dbg_pc >= 0x02001130 && dbg_pc <= 0x02001160) {
            fprintf(stderr, "[HOOK-INTR-1142] intno=%d PC=0x%08x\n", intno, dbg_pc);
        }
    }

    /* A-line exception (interrupt #10) */
    if (intno == 10) {
        uint32_t pc;
        uint16_t opcode;

        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_mem_read(uc, pc, &opcode, sizeof(opcode));
        opcode = (opcode >> 8) | (opcode << 8);

        /* EmulOp range (0xAE00-0xAE3F) */
        if ((opcode & 0xFFC0) == 0xAE00) {
            uint16_t legacy_opcode = 0x7100 | (opcode & 0x3F);

            /* Check for EXEC_RETURN (0xAE00 = M68K_EXEC_RETURN sentinel) */
            if (legacy_opcode == 0x7100) {
                /* Signal Execute68kTrap that the trap has returned */
                extern volatile bool g_exec68k_return_flag;
                g_exec68k_return_flag = true;
                /* Advance PC past the sentinel */
                pc += 2;
                uc_reg_write(uc, UC_M68K_REG_PC, &pc);
                /* Stop execution to return to Execute68kTrap */
                uc_emu_stop(uc);
                return;
            }

            /* Trace ALL EmulOps after FIX_MEMSIZE to see boot progress */
            if (g_post_fixmem_blocks > 0) {
                static int post_fixmem_emulop_count = 0;
                if (post_fixmem_emulop_count++ < 200) {
                    fprintf(stderr, "[POST-FIXMEM-EMULOP] #%d AE opcode=0x%04x -> legacy=0x%04x at PC=0x%08x\n",
                            post_fixmem_emulop_count, opcode, legacy_opcode, pc);
                }
            }

            if (g_platform.emulop_handler) {
                /* Specific IRQ EmulOp tracking (0x7129 = M68K_EMUL_OP_IRQ) */
                if (legacy_opcode == 0x7129) {
                    static int irq_emulop_trace = 0;
                    if (++irq_emulop_trace <= 10) {
                        uint32_t d0=0, sr_val=0;
                        uc_reg_read(uc, UC_M68K_REG_D0, &d0);
                        uc_reg_read(uc, UC_M68K_REG_SR, &sr_val);
                        fprintf(stderr, "[EMULOP-IRQ] #%d at PC=0x%08x D0=0x%08x SR=0x%04x (before handler)\n",
                                irq_emulop_trace, pc, d0, sr_val & 0xFFFF);
                    }
                }

                bool pc_advanced = g_platform.emulop_handler(legacy_opcode, false);

                /* Post-handler IRQ tracking */
                if (legacy_opcode == 0x7129) {
                    static int irq_emulop_post = 0;
                    if (++irq_emulop_post <= 10) {
                        /* Read Ticks at 0x16a to verify increment */
                        uint8_t tb[4] = {0};
                        uc_mem_read(uc, 0x16a, tb, 4);
                        uint32_t ticks = (tb[0]<<24)|(tb[1]<<16)|(tb[2]<<8)|tb[3];
                        uint32_t d0_after=0;
                        uc_reg_read(uc, UC_M68K_REG_D0, &d0_after);
                        fprintf(stderr, "[EMULOP-IRQ] #%d at PC=0x%08x D0=0x%08x Ticks=%u pc_advanced=%d (after handler)\n",
                                irq_emulop_post, pc, d0_after, ticks, pc_advanced);
                    }
                }

                if (!pc_advanced) {
                    pc += 2;
                    uc_reg_write(uc, UC_M68K_REG_PC, &pc);
                }

                /* Trace: detect FIX_MEMSIZE (0x7109 = 0xAE09) */
                if (legacy_opcode == 0x7109) {
                    g_post_fixmem_blocks = 1;
                    uint32_t post_pc = 0;
                    uc_reg_read(uc, UC_M68K_REG_PC, &post_pc);
                    fprintf(stderr, "[FIX_MEMSIZE] Handled! PC after=0x%08x\n", post_pc);

                    /* Dump key OS trap table entries after FIX_MEMSIZE */
                    uint8_t table[16];
                    /* InsTime = 0x58 -> table[0x0560] */
                    uc_mem_read(uc, 0x0560, table, 4);
                    uint32_t instime_handler = (table[0]<<24)|(table[1]<<16)|(table[2]<<8)|table[3];
                    /* RmvTime = 0x59 -> table[0x0564] */
                    uc_mem_read(uc, 0x0564, table, 4);
                    uint32_t rmvtime_handler = (table[0]<<24)|(table[1]<<16)|(table[2]<<8)|table[3];
                    /* PrimeTime = 0x5A -> table[0x0568] */
                    uc_mem_read(uc, 0x0568, table, 4);
                    uint32_t primetime_handler = (table[0]<<24)|(table[1]<<16)|(table[2]<<8)|table[3];
                    /* IRQ handler = 0xA029 -> table[0x04A4] (actually the IRQ trap is patched in ROM) */
                    /* SCSI_DISPATCH, INSTALL_DRIVERS etc. */
                    fprintf(stderr, "[FIX_MEMSIZE] OS trap table: InsTime[0x58]=0x%08x RmvTime[0x59]=0x%08x PrimeTime[0x5A]=0x%08x\n",
                            instime_handler, rmvtime_handler, primetime_handler);
                    /* Also check what's at the InsTime handler address */
                    if (instime_handler >= 0x02000000 && instime_handler < 0x02100000) {
                        uint8_t code[4] = {0};
                        uc_mem_read(uc, instime_handler, code, 4);
                        fprintf(stderr, "[FIX_MEMSIZE] Code at InsTime handler 0x%08x: %02x%02x %02x%02x\n",
                                instime_handler, code[0], code[1], code[2], code[3]);
                    }
                }

                /* CRITICAL: Apply deferred updates IMMEDIATELY after EmulOp.
                 * If the next instruction causes an exception (like an A-trap),
                 * deferred updates would never be applied by hook_block. */
                apply_deferred_updates_and_flush(cpu, uc, "hook_interrupt");
            }
        }
        /* Other A-line traps: let QEMU's exception mechanism handle them */
    }
}

/**
 * Hook for invalid instructions (UC_HOOK_INSN_INVALID)
 * Handles legacy 0x71xx EmulOps only.
 *
 * NOTE: A-line and F-line traps are now handled via UC_HOOK_INTR + cpu-exec.c,
 * not here. This avoids duplicate handling and uses proper QEMU exception mechanism.
 */
static bool hook_insn_invalid(uc_engine *uc, void *user_data) {
    uint32_t pc;
    uint16_t opcode;

    /* Get current PC and read opcode */
    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));
    opcode = (opcode >> 8) | (opcode << 8);  /* Swap for big-endian */

    /* Check if EmulOp (0x71xx) - legacy format */
    if ((opcode & 0xFF00) == 0x7100) {
        if (g_platform.emulop_handler) {
            bool pc_advanced = g_platform.emulop_handler(opcode, false);

            /* Advance PC if handler didn't */
            if (!pc_advanced) {
                pc += 2;
                uc_reg_write(uc, UC_M68K_REG_PC, &pc);
            }
            return true;  /* Continue execution */
        }
    }

    /* For A-line (0xAxxx) and F-line (0xFxxx):
     * These are handled by UC_HOOK_INTR (exception 10/11) + cpu-exec.c now.
     * If we reach here with A/F-line, it means Unicorn detected it as invalid
     * instruction rather than exception - just return false to stop. */

    return false;  /* Stop execution - truly invalid instruction */
}

/**
 * Memory trace hook for CPU tracing
 */
static bool hook_mem_trace(uc_engine *uc, uc_mem_type type,
                           uint64_t address, int size, int64_t value,
                           void *user_data) {
    if (type != UC_MEM_READ) return true;

    uint32_t val = 0;
    uc_mem_read(uc, address, &val, size);

    /* Byte-swap if needed (M68K is big-endian) */
    if (size == 2) {
        val = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
    } else if (size == 4) {
        val = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
              ((val & 0xFF0000) >> 8) | ((val >> 24) & 0xFF);
    }

    cpu_trace_log_mem_read((uint32_t)address, val, size);
    return true;
}

/* Create Unicorn CPU with specified model */
UnicornCPU *unicorn_create_with_model(UnicornArch arch, int cpu_model) {
    UnicornCPU *cpu = calloc(1, sizeof(UnicornCPU));
    if (!cpu) return NULL;

    cpu->arch = arch;

    /* Initialize block statistics */
    cpu->block_stats.min_block_size = UINT32_MAX;

    /* Create Unicorn engine */
    uc_mode mode = (arch == UCPU_ARCH_M68K) ? UC_MODE_BIG_ENDIAN : UC_MODE_LITTLE_ENDIAN;
    uc_err err = uc_open(UC_ARCH_M68K, mode, &cpu->uc);

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to create Unicorn: %s", uc_strerror(err));
        free(cpu);
        return NULL;
    }

    /* Set CPU model */
    uc_ctl_set_cpu_model(cpu->uc, cpu_model);

    /* Register hooks */

    /* Block hook for interrupts */
    err = uc_hook_add(cpu->uc, &cpu->block_hook,
                     UC_HOOK_BLOCK,
                     (void*)hook_block,
                     cpu, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to register block hook: %s\n", uc_strerror(err));
        uc_close(cpu->uc);
        free(cpu);
        return NULL;
    }

    /* Invalid instruction hook for legacy EmulOps */
    err = uc_hook_add(cpu->uc, &cpu->insn_invalid_hook,
                     UC_HOOK_INSN_INVALID,
                     (void*)hook_insn_invalid,
                     cpu, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to register invalid instruction hook: %s\n", uc_strerror(err));
        uc_close(cpu->uc);
        free(cpu);
        return NULL;
    }

    /* Interrupt hook for A-line EmulOps */
    err = uc_hook_add(cpu->uc, &cpu->intr_hook,
                     UC_HOOK_INTR,
                     (void*)hook_interrupt,
                     cpu, 1, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to register interrupt hook: %s\n", uc_strerror(err));
        uc_close(cpu->uc);
        free(cpu);
        return NULL;
    }

    return cpu;
}

void unicorn_destroy(UnicornCPU *cpu) {
    if (!cpu) return;

    if (cpu->uc) {
        uc_close(cpu->uc);
    }

    free(cpu);
}

/* Error handling */
const char *unicorn_get_error(UnicornCPU *cpu) {
    return cpu ? cpu->error : "NULL CPU";
}

/* Execution */
bool unicorn_execute(UnicornCPU *cpu, uint64_t start, uint64_t until, uint64_t timeout, size_t count) {
    if (!cpu || !cpu->uc) return false;

    static int execute_count = 0;
    if (++execute_count <= 5) {
        fprintf(stderr, "[unicorn_execute #%d] start=0x%08lx, until=0x%08lx, timeout=%lu, count=%zu\n",
                execute_count, start, until, timeout, count);
    }

    uc_err err = uc_emu_start(cpu->uc, start, until, timeout, count);

    if (execute_count <= 5) {
        fprintf(stderr, "[unicorn_execute #%d] returned with err=%d (%s)\n",
                execute_count, err, uc_strerror(err));
    }

    /* Apply any deferred register updates and flush cache */
    apply_deferred_updates_and_flush(cpu, cpu->uc, "unicorn_execute");

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Execution failed: %s", uc_strerror(err));
        return false;
    }

    return true;
}

/* Memory operations */
bool unicorn_map_memory(UnicornCPU *cpu, uint64_t address, size_t size, uint32_t perms) {
    if (!cpu || !cpu->uc) return false;

    uc_err err = uc_mem_map(cpu->uc, address, size, perms);
    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to map memory at 0x%llx: %s",
                (unsigned long long)address, uc_strerror(err));
        return false;
    }

    return true;
}

bool unicorn_write_memory(UnicornCPU *cpu, uint64_t address, const void *data, size_t size) {
    if (!cpu || !cpu->uc) return false;

    uc_err err = uc_mem_write(cpu->uc, address, data, size);
    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to write memory at 0x%llx: %s",
                (unsigned long long)address, uc_strerror(err));
        return false;
    }

    return true;
}

bool unicorn_read_memory(UnicornCPU *cpu, uint64_t address, void *data, size_t size) {
    if (!cpu || !cpu->uc) return false;

    uc_err err = uc_mem_read(cpu->uc, address, data, size);
    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to read memory at 0x%llx: %s",
                (unsigned long long)address, uc_strerror(err));
        return false;
    }

    return true;
}

/* Register access for M68K */
uint32_t unicorn_get_pc(UnicornCPU *cpu) {
    uint32_t pc = 0;
    if (cpu && cpu->uc) {
        uc_reg_read(cpu->uc, UC_M68K_REG_PC, &pc);
    }
    return pc;
}

void unicorn_set_pc(UnicornCPU *cpu, uint32_t pc) {
    if (cpu && cpu->uc) {
        /* uc_reg_write() automatically flushes cache when writing to PC.
         * No manual flushing needed - Unicorn handles it internally. */
        uc_reg_write(cpu->uc, UC_M68K_REG_PC, &pc);
    }
}

uint32_t unicorn_get_dreg(UnicornCPU *cpu, int reg) {
    uint32_t val = 0;
    if (cpu && cpu->uc && reg >= 0 && reg <= 7) {
        uc_reg_read(cpu->uc, UC_M68K_REG_D0 + reg, &val);
    }
    return val;
}

void unicorn_set_dreg(UnicornCPU *cpu, int reg, uint32_t val) {
    if (cpu && cpu->uc && reg >= 0 && reg <= 7) {
        /* Data register writes do NOT require cache flushing.
         * Register values are read from CPU state at runtime, not baked into TBs. */
        uc_reg_write(cpu->uc, UC_M68K_REG_D0 + reg, &val);
    }
}

uint32_t unicorn_get_areg(UnicornCPU *cpu, int reg) {
    uint32_t val = 0;
    if (cpu && cpu->uc && reg >= 0 && reg <= 7) {
        uc_reg_read(cpu->uc, UC_M68K_REG_A0 + reg, &val);
    }
    return val;
}

void unicorn_set_areg(UnicornCPU *cpu, int reg, uint32_t val) {
    if (cpu && cpu->uc && reg >= 0 && reg <= 7) {
        /* Address register writes do NOT require cache flushing.
         * Register values are read from CPU state at runtime, not baked into TBs. */
        uc_reg_write(cpu->uc, UC_M68K_REG_A0 + reg, &val);
    }
}

uint16_t unicorn_get_sr(UnicornCPU *cpu) {
    uint32_t sr = 0;  /* Use uint32_t for uc_reg_read */
    if (cpu && cpu->uc) {
        uc_reg_read(cpu->uc, UC_M68K_REG_SR, &sr);
    }
    return (uint16_t)sr;  /* Cast to uint16_t for return */
}

void unicorn_set_sr(UnicornCPU *cpu, uint16_t sr) {
    if (cpu && cpu->uc) {
        /* SR (Status Register) writes do NOT require cache flushing.
         * While SR affects interrupt masking and supervisor mode, these are
         * checked at runtime by the CPU, not baked into translated blocks.
         * Only CODE modification requires cache flushing, not register changes. */
        uint32_t sr32 = sr;  /* Convert to uint32_t for uc_reg_write */
        uc_reg_write(cpu->uc, UC_M68K_REG_SR, &sr32);
    }
}

/* Interrupt triggering */
void unicorn_trigger_interrupt_internal(int level) {
    g_pending_interrupt_level = level;
    static int trigger_count = 0;
    if (++trigger_count <= 10) {
        fprintf(stderr, "[unicorn_trigger_interrupt_internal] Setting g_pending_interrupt_level to %d\n", level);
    }
}

/* Get Unicorn engine handle */
void *unicorn_get_uc(UnicornCPU *cpu) {
    return cpu ? cpu->uc : NULL;
}

/* Enable CPU tracing */
void unicorn_enable_tracing(UnicornCPU *cpu, bool enable) {
    if (!cpu || !cpu->uc) return;

    if (enable && !cpu->trace_hook) {
        uc_hook_add(cpu->uc, &cpu->trace_hook,
                   UC_HOOK_MEM_READ,
                   (void*)hook_mem_trace,
                   NULL, 1, 0);
    } else if (!enable && cpu->trace_hook) {
        uc_hook_del(cpu->uc, cpu->trace_hook);
        cpu->trace_hook = 0;
    }
}

/* Print block statistics */
void unicorn_print_block_stats(UnicornCPU *cpu) {
    if (!cpu) return;

    BlockStats *stats = &cpu->block_stats;

    printf("\n=== Unicorn Block Execution Statistics ===\n");
    printf("Total blocks executed:      %llu\n", (unsigned long long)stats->total_blocks);
    printf("Total instructions:         %llu\n", (unsigned long long)stats->total_instructions);

    if (stats->total_blocks > 0) {
        double avg = (double)stats->sum_block_sizes / stats->total_blocks;
        printf("Average block size:         %.2f instructions\n", avg);
        printf("Min block size:             %u instructions\n", stats->min_block_size);
        printf("Max block size:             %u instructions\n", stats->max_block_size);

        /* Print distribution */
        printf("\nBlock Size Distribution:\n");
        printf("Size (insns) | Count        | Percentage | Cumulative\n");
        printf("-------------|--------------|------------|-----------\n");

        uint64_t cumulative = 0;
        for (int i = 1; i <= 100; i++) {
            if (stats->block_size_histogram[i] > 0) {
                cumulative += stats->block_size_histogram[i];
                double pct = (double)stats->block_size_histogram[i] * 100.0 / stats->total_blocks;
                double cum_pct = (double)cumulative * 100.0 / stats->total_blocks;

                if (i < 100) {
                    printf("%-12d | %12llu | %9.2f%% | %9.2f%%\n",
                          i, (unsigned long long)stats->block_size_histogram[i],
                          pct, cum_pct);
                } else {
                    printf("100+         | %12llu | %9.2f%% | %9.2f%%\n",
                          (unsigned long long)stats->block_size_histogram[i],
                          pct, cum_pct);
                }
            }
        }
    }
    printf("==========================================\n");
}

/* ========================================
 * Additional wrapper functions
 * ========================================*/

/* Memory mapping with endianness handling */
bool unicorn_map_ram(UnicornCPU *cpu, uint64_t addr, void *host_ptr, uint64_t size) {
    if (!cpu || !cpu->uc) return false;

    /* Map the memory region */
    uc_err err = uc_mem_map_ptr(cpu->uc, addr, size, UC_PROT_ALL, host_ptr);

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to map RAM at 0x%llx: %s",
                (unsigned long long)addr, uc_strerror(err));
        return false;
    }

    return true;
}

bool unicorn_map_rom(UnicornCPU *cpu, uint64_t addr, const void *host_ptr, uint64_t size) {
    if (!cpu || !cpu->uc) return false;

    /* Map as read-only */
    uc_err err = uc_mem_map_ptr(cpu->uc, addr, size, UC_PROT_READ | UC_PROT_EXEC, (void*)host_ptr);

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to map ROM at 0x%llx: %s",
                (unsigned long long)addr, uc_strerror(err));
        return false;
    }

    return true;
}

bool unicorn_map_rom_writable(UnicornCPU *cpu, uint64_t addr, const void *host_ptr, uint64_t size) {
    if (!cpu || !cpu->uc) return false;

    /* Map as read-write (for debugging/validation) */
    uc_err err = uc_mem_map_ptr(cpu->uc, addr, size, UC_PROT_ALL, (void*)host_ptr);

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to map ROM writable at 0x%llx: %s",
                (unsigned long long)addr, uc_strerror(err));
        return false;
    }

    return true;
}

bool unicorn_unmap(UnicornCPU *cpu, uint64_t addr, uint64_t size) {
    if (!cpu || !cpu->uc) return false;

    uc_err err = uc_mem_unmap(cpu->uc, addr, size);

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Failed to unmap memory at 0x%llx: %s",
                (unsigned long long)addr, uc_strerror(err));
        return false;
    }

    return true;
}

/* Extended memory operations */
bool unicorn_mem_write(UnicornCPU *cpu, uint64_t addr, const void *data, size_t size) {
    return unicorn_write_memory(cpu, addr, data, size);
}

bool unicorn_mem_read(UnicornCPU *cpu, uint64_t addr, void *data, size_t size) {
    return unicorn_read_memory(cpu, addr, data, size);
}

/* Execute single instruction */
bool unicorn_execute_one(UnicornCPU *cpu) {
    if (!cpu || !cpu->uc) return false;

    uint32_t pc = unicorn_get_pc(cpu);

    /* Execute exactly one instruction */
    uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, 1);

    /* Apply any deferred register updates and flush cache */
    apply_deferred_updates_and_flush(cpu, cpu->uc, "unicorn_execute_one");

    if (err != UC_ERR_OK) {
        snprintf(cpu->error, sizeof(cpu->error),
                "Single step failed at PC=0x%08x: %s",
                pc, uc_strerror(err));
        return false;
    }

    return true;
}

/* Execute N instructions */
bool unicorn_execute_n(UnicornCPU *cpu, uint64_t count) {
    if (!cpu || !cpu->uc) return false;

    uint32_t pc = unicorn_get_pc(cpu);

    static int execute_n_count = 0;
    if (++execute_n_count <= 10) {
        fprintf(stderr, "[unicorn_execute_n #%d] PC=0x%08x, count=%lu\n",
                execute_n_count, pc, count);
        fflush(stderr);
    }

    /* Execute specified number of instructions */
    uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, count);

    if (execute_n_count <= 10) {
        fprintf(stderr, "[unicorn_execute_n #%d] returned err=%d (%s)\n",
                execute_n_count, err, uc_strerror(err));
        fflush(stderr);
    }

    /* Apply any deferred register updates and flush cache */
    apply_deferred_updates_and_flush(cpu, cpu->uc, "unicorn_execute_n");

    if (err != UC_ERR_OK) {
        /* Check if this is from uc_emu_stop() - that returns UC_ERR_OK actually */
        snprintf(cpu->error, sizeof(cpu->error),
                "Execution failed at PC=0x%08x: %s (err=%d)",
                pc, uc_strerror(err), err);
        return false;
    }

    return true;
}

/* Control registers */
uint32_t unicorn_get_vbr(UnicornCPU *cpu) {
    uint32_t vbr = 0;
    if (cpu && cpu->uc) {
        uc_reg_read(cpu->uc, UC_M68K_REG_CR_VBR, &vbr);
    }
    return vbr;
}

void unicorn_set_vbr(UnicornCPU *cpu, uint32_t vbr) {
    if (cpu && cpu->uc) {
        uc_reg_write(cpu->uc, UC_M68K_REG_CR_VBR, &vbr);
    }
}

uint32_t unicorn_get_cacr(UnicornCPU *cpu) {
    uint32_t cacr = 0;
    if (cpu && cpu->uc) {
        uc_reg_read(cpu->uc, UC_M68K_REG_CR_CACR, &cacr);
    }
    return cacr;
}

void unicorn_set_cacr(UnicornCPU *cpu, uint32_t cacr) {
    if (cpu && cpu->uc) {
        uc_reg_write(cpu->uc, UC_M68K_REG_CR_CACR, &cacr);
    }
}

/* Default arch wrapper */
UnicornCPU* unicorn_create(UnicornArch arch) {
    return unicorn_create_with_model(arch, -1);  /* Use default CPU model */
}

/* Phase 2: Helper functions for QEMU-style execution loop */

/* Poll for interrupts and deliver if needed */
bool unicorn_poll_interrupts(UnicornCPU *cpu) {
    if (!cpu || !cpu->uc) return false;

    extern uint64_t poll_timer_interrupt(void);
    extern volatile int g_pending_interrupt_level;

    // Check timer interrupts
    uint64_t expirations = poll_timer_interrupt();

    // If we have pending interrupts, we need to handle them
    if (g_pending_interrupt_level > 0) {
        // Get current SR to check interrupt mask
        uint32_t sr = 0;
        uc_reg_read(cpu->uc, UC_M68K_REG_SR, &sr);
        int current_ipl = (sr >> 8) & 7;

        // Check if interrupt is not masked
        if (g_pending_interrupt_level > current_ipl) {
            // The interrupt hook will handle the actual delivery
            // We just need to signal that an interrupt occurred
            return true;
        }
    }

    return expirations > 0;
}

/* Handle illegal instruction */
bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc) {
    if (!cpu || !cpu->uc) return false;

    // Read the opcode
    uint16_t opcode;
    if (uc_mem_read(cpu->uc, pc, &opcode, 2) != UC_ERR_OK) {
        return false;
    }
    opcode = __builtin_bswap16(opcode);

    // Check if it's an EmulOp (0x71xx)
    if ((opcode & 0xFF00) == 0x7100) {
        // EmulOps should be handled by the interrupt hook
        // If we get here, something went wrong
        fprintf(stderr, "[unicorn_handle_illegal] Unhandled EmulOp 0x%04x at PC 0x%08x\n",
                opcode, pc);

        // Try to skip past it
        pc += 2;
        uc_reg_write(cpu->uc, UC_M68K_REG_PC, &pc);
        return true;
    }

    // Check for A-line (0xAxxx) or F-line (0xFxxx) traps
    if ((opcode & 0xF000) == 0xA000 || (opcode & 0xF000) == 0xF000) {
        // These should be handled by exception handlers
        // For now, skip past them
        pc += 2;
        uc_reg_write(cpu->uc, UC_M68K_REG_PC, &pc);
        return true;
    }

    // Real illegal instruction - can't handle
    return false;
}
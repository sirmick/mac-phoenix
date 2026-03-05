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

/* Shared M68K register structure (C-compatible) */
#include "m68k_registers.h"

/* C bridge for calling C++ EmulOp from C code */
extern void EmulOp_C(uint16_t opcode, M68kRegisters *r);

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


/**
 * Hook for block execution (UC_HOOK_BLOCK)
 * Called at the start of each translation block.
 *
 * This is the hottest path in the emulator. Keep it lean.
 * Essential responsibilities:
 *   1. Block statistics
 *   2. Interrupt acknowledge (after delivery)
 *   3. Stale TB detection (SMC safety net)
 *   4. SCSI timeout accelerator
 *   5. Timer polling (every 4096 blocks)
 *   6. Apply deferred register updates
 *   7. PC-based tracing trigger
 *   8. Pending interrupt delivery
 */
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    /* --- 1. Block statistics --- */
    cpu->block_stats.total_blocks++;
    cpu->block_stats.total_instructions += size;
    if (size < cpu->block_stats.min_block_size) cpu->block_stats.min_block_size = size;
    if (size > cpu->block_stats.max_block_size) cpu->block_stats.max_block_size = size;
    cpu->block_stats.sum_block_sizes += size;
    if (size <= 100) cpu->block_stats.block_size_histogram[size]++;
    else cpu->block_stats.block_size_histogram[100]++;

    /* --- 2. Interrupt acknowledge --- */
    /* After uc_m68k_trigger_interrupt() + uc_emu_stop(), QEMU delivers the
     * interrupt on the next uc_emu_start(). The first hook_block after that
     * is in the handler with IPL raised. Clear QEMU's pending_level to
     * prevent re-delivery after RTE (mimics hardware interrupt ack). */
    if (g_interrupt_pending_ack) {
        g_interrupt_pending_ack = false;
        uc_m68k_trigger_interrupt(uc, 0, 0);
    }

    /* --- 3. Stale TB detector (SMC safety net) ---
     * For blocks in the critical patch area (0x0001C000-0x0001D000),
     * verify memory matches what was first seen when the TB was compiled.
     * If memory changed, the JIT has a stale TB — flush and re-translate.
     * This catches the ~18 edge cases that notdirty_write() misses.
     * See docs/deepdive/JIT_SMC_Detection_Analysis.md */
    if (address >= 0x0001C000 && address < 0x0001D000 &&
        cpu->block_stats.total_blocks > 100000000ULL) {
        static struct { uint32_t addr; uint32_t first_bytes; uint64_t first_block; } seen[128];
        static int seen_count = 0;
        uint8_t mem[4] = {0};
        uc_mem_read(uc, address, mem, 4);
        uint32_t cur_bytes = (mem[0]<<24)|(mem[1]<<16)|(mem[2]<<8)|mem[3];

        int found = -1;
        for (int si = 0; si < seen_count; si++) {
            if (seen[si].addr == (uint32_t)address) { found = si; break; }
        }
        if (found >= 0 && cur_bytes != seen[found].first_bytes) {
            uc_ctl_flush_tb(uc);
            seen[found].first_bytes = cur_bytes;
            seen[found].first_block = cpu->block_stats.total_blocks;
        } else if (found < 0 && seen_count < 128) {
            seen[seen_count].addr = (uint32_t)address;
            seen[seen_count].first_bytes = cur_bytes;
            seen[seen_count].first_block = cpu->block_stats.total_blocks;
            seen_count++;
        }
    }

    /* --- 4. SCSI timeout accelerator ---
     * ROM SCSI probe at 0x020014c0 busy-waits reading Mac Ticks ($016A):
     *   0x14ca: cmpl $016A.w, D0   ; compare D0 with Ticks counter
     *   0x14ce: bccs 0x14ca        ; loop until Ticks >= D0
     * This burns millions of blocks. Skip the wait by advancing Ticks directly.
     *
     * Also cap D5 (outer loop counter) to prevent 545-second SCSI timeout. */
    if (address == 0x020014be || address == 0x020014c0 || address == 0x020014ca) {
        /* Cap D5 to prevent excessive SCSI timeout (240 = 4 seconds max) */
        if (address == 0x020014be || address == 0x020014c0) {
            uint32_t d5 = 0;
            uc_reg_read(uc, UC_M68K_REG_D5, &d5);
            if (d5 > 240) {
                d5 = 240;
                uc_reg_write(uc, UC_M68K_REG_D5, &d5);
            }
        }
        /* Fast-forward Ticks to break busy-wait loop */
        if (address == 0x020014ca) {
            uint32_t d0 = 0;
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            /* Read current Ticks from Mac low memory ($016A) */
            extern uint8_t *RAMBaseHost;
            if (RAMBaseHost) {
                uint8_t *tp = RAMBaseHost + 0x016A;
                uint32_t ticks = (tp[0]<<24)|(tp[1]<<16)|(tp[2]<<8)|tp[3];
                if (ticks < d0) {
                    /* Set Ticks = D0 to break the loop immediately */
                    uint32_t new_ticks = d0;
                    tp[0] = (new_ticks >> 24) & 0xFF;
                    tp[1] = (new_ticks >> 16) & 0xFF;
                    tp[2] = (new_ticks >> 8) & 0xFF;
                    tp[3] = new_ticks & 0xFF;
                }
            }
        }
    }

    /* --- 5. Timer polling (every 4096 blocks) ---
     * At ~60Hz timer rate, polling ~500-2500x/second is plenty.
     * 4096 blocks * ~6 insns/block = ~25K instructions between polls. */
    extern uint64_t poll_timer_interrupt(void);
    if ((cpu->block_stats.total_blocks & 0xFFF) == 0) {
        poll_timer_interrupt();
    }

    /* --- 6. Apply deferred register updates ---
     * EmulOp handlers defer register changes that must be applied
     * before the next instruction executes. */
    apply_deferred_updates_and_flush(cpu, uc, "hook_block");

    /* --- 7. PC-based tracing trigger --- */
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

    /* --- 8. Pending interrupt delivery --- */
    if (g_pending_interrupt_level > 0) {
        uint32_t sr = 0;
        uc_reg_read(uc, UC_M68K_REG_SR, &sr);
        int current_ipl = ((sr & 0xFFFF) >> 8) & 7;

        if (g_pending_interrupt_level > current_ipl) {
            uint8_t vector = (g_pending_interrupt_level == 1) ? 0x19 :
                           (0x18 + g_pending_interrupt_level);

            uc_m68k_trigger_interrupt(uc, g_pending_interrupt_level, vector);
            g_pending_interrupt_level = 0;
            g_interrupt_pending_ack = true;
            uc_emu_stop(uc);
        }
    }
}

/**
 * Hook for CPU exceptions (UC_HOOK_INTR)
 * Handles A-line exceptions for EmulOps (0xAE00-0xAE3F)
 */
static void hook_interrupt(uc_engine *uc, uint32_t intno, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    /* A-line exception (interrupt #10) */
    if (intno == 10) {
        uint32_t pc;
        uint16_t opcode;

        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_mem_read(uc, pc, &opcode, sizeof(opcode));
        opcode = (opcode >> 8) | (opcode << 8);  /* byte-swap (M68K big-endian) */

        /* EmulOp range (0xAE00-0xAE3F) */
        if ((opcode & 0xFFC0) == 0xAE00) {
            uint16_t legacy_opcode = 0x7100 | (opcode & 0x3F);

            /* EXEC_RETURN sentinel (0xAE00 = M68K_EXEC_RETURN) */
            if (legacy_opcode == 0x7100) {
                extern volatile bool g_exec68k_return_flag;
                g_exec68k_return_flag = true;
                pc += 2;
                uc_reg_write(uc, UC_M68K_REG_PC, &pc);
                uc_emu_stop(uc);
                return;
            }

            if (g_platform.emulop_handler) {
                bool pc_advanced = g_platform.emulop_handler(legacy_opcode, false);

                if (!pc_advanced) {
                    pc += 2;
                    uc_reg_write(uc, UC_M68K_REG_PC, &pc);
                }

                /* Apply deferred updates IMMEDIATELY after EmulOp.
                 * If the next instruction causes an exception (like an A-trap),
                 * deferred updates would never be applied by hook_block. */
                apply_deferred_updates_and_flush(cpu, uc, "hook_interrupt");
            }
        }
        /* Other A-line traps: QEMU handles the exception natively
         * (pushes stack frame, jumps to LINE-A vector handler) */
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

    uc_err err = uc_emu_start(cpu->uc, start, until, timeout, count);

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

    /* Execute specified number of instructions */
    uc_err err = uc_emu_start(cpu->uc, pc, 0, 0, count);

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
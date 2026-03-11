#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/**
 * Unicorn Engine Wrapper Implementation (Cleaned Version)
 *
 * Provides M68K emulation using Unicorn engine with A-line EmulOp support.
 * All obsolete MMIO transport code has been removed.
 */

#include "unicorn_wrapper.h"
#include "platform.h"
#include "cpu_trace.h"
#include <stdlib.h>
#include "timer_interrupt.h"
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static inline uint64_t perf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

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


/* Performance counters */
typedef struct {
    uint64_t emu_start_ns;          /* Time inside uc_emu_start() */
    uint64_t emulop_count;          /* Number of EmulOp dispatches */
    uint64_t interrupt_count;       /* Number of interrupt deliveries */
    uint64_t emu_start_count;       /* Number of uc_emu_start() calls */
    uint64_t hook_block_ns;         /* Time inside hook_block() */
    uint64_t hook_block_count;      /* Number of hook_block calls */
    uint64_t hook_interrupt_ns;     /* Time inside hook_interrupt() */
    uint64_t deferred_update_count; /* How many times deferred updates applied */
    uint64_t flush_code_cache_count;/* FlushCodeCache calls */
} PerfCounters;

/* Block counter (used for timer polling interval) */
typedef struct {
    uint64_t total_blocks;
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

    /* Performance counters */
    PerfCounters perf;

    /* Deferred SR update */
    bool has_deferred_sr_update;
    uint16_t deferred_sr_value;

    /* Deferred register updates (D0-D7, A0-A7) */
    bool has_deferred_dreg_update[8];
    uint32_t deferred_dreg_value[8];
    bool has_deferred_areg_update[8];
    uint32_t deferred_areg_value[8];
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
static bool apply_deferred_updates_and_flush(UnicornCPU *cpu, uc_engine *uc, const char *caller __attribute__((unused))) {
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
 * This is the hottest path in the emulator — minimize work per call.
 * Responsibilities:
 *   1. Interrupt acknowledge (after delivery, redundant with auto-ack but harmless)
 *   2. SCSI timeout accelerator
 *   3. Timer polling (every 4096 blocks)
 *   4. Apply deferred register updates
 *   5. Pending interrupt delivery
 */
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size __attribute__((unused)), void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;
    uint64_t t0 = perf_now_ns();
    cpu->block_stats.total_blocks++;

    /* --- 1. Interrupt acknowledge --- */
    if (g_interrupt_pending_ack) {
        g_interrupt_pending_ack = false;
        uc_m68k_trigger_interrupt(uc, 0, 0);
    }

    /* --- 2. SCSI timeout accelerator ---
     * ROM SCSI probe busy-waits reading Mac Ticks ($016A).
     * Cap D5 and fast-forward Ticks to skip the wait. */
    if (address == 0x020014be || address == 0x020014c0 || address == 0x020014ca) {
        if (address == 0x020014be || address == 0x020014c0) {
            uint32_t d5 = 0;
            uc_reg_read(uc, UC_M68K_REG_D5, &d5);
            if (d5 > 240) {
                d5 = 240;
                uc_reg_write(uc, UC_M68K_REG_D5, &d5);
            }
        }
        if (address == 0x020014ca) {
            uint32_t d0 = 0;
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            extern uint8_t *RAMBaseHost;
            if (RAMBaseHost) {
                uint8_t *tp = RAMBaseHost + 0x016A;
                uint32_t ticks = (tp[0]<<24)|(tp[1]<<16)|(tp[2]<<8)|tp[3];
                if (ticks < d0) {
                    uint32_t new_ticks = d0;
                    tp[0] = (new_ticks >> 24) & 0xFF;
                    tp[1] = (new_ticks >> 16) & 0xFF;
                    tp[2] = (new_ticks >> 8) & 0xFF;
                    tp[3] = new_ticks & 0xFF;
                }
            }
        }
    }

    /* --- 3. Timer polling (every 4096 blocks) --- */
    extern uint64_t poll_timer_interrupt(void);
    if ((cpu->block_stats.total_blocks & 0xFFF) == 0) {
        poll_timer_interrupt();
    }

    /* --- 4. Apply deferred register updates --- */
    if (apply_deferred_updates_and_flush(cpu, uc, "hook_block"))
        cpu->perf.deferred_update_count++;

    /* --- 5. Pending interrupt delivery --- */
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
            cpu->perf.interrupt_count++;
        }
    }

    cpu->perf.hook_block_ns += perf_now_ns() - t0;
    cpu->perf.hook_block_count++;
}

/**
 * Hook for CPU exceptions (UC_HOOK_INTR)
 * Handles A-line exceptions for EmulOps (0xAE00-0xAE3F)
 */
static void hook_interrupt(uc_engine *uc, uint32_t intno, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;
    uint64_t t0 = perf_now_ns();

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

                apply_deferred_updates_and_flush(cpu, uc, "hook_interrupt");
            }
        }
    }

    cpu->perf.emulop_count++;
    cpu->perf.hook_interrupt_ns += perf_now_ns() - t0;
}

/**
 * Hook for invalid instructions (UC_HOOK_INSN_INVALID)
 * Handles legacy 0x71xx EmulOps only.
 *
 * NOTE: A-line and F-line traps are now handled via UC_HOOK_INTR + cpu-exec.c,
 * not here. This avoids duplicate handling and uses proper QEMU exception mechanism.
 */
static bool hook_insn_invalid(uc_engine *uc, void *user_data __attribute__((unused))) {
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
                           uint64_t address, int size, int64_t value __attribute__((unused)),
                           void *user_data __attribute__((unused))) {
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

/* Performance counter helpers */
void unicorn_perf_add_emu_start(UnicornCPU *cpu, uint64_t ns) {
    if (cpu) {
        cpu->perf.emu_start_ns += ns;
        cpu->perf.emu_start_count++;
    }
}

/* Print performance counters */
void unicorn_print_perf_counters(UnicornCPU *cpu) {
    if (!cpu) return;
    PerfCounters *p = &cpu->perf;

    fprintf(stderr, "\n=== Unicorn Performance Counters ===\n");

    double total_s = (double)p->emu_start_ns / 1e9;
    double hook_block_s = (double)p->hook_block_ns / 1e9;
    double hook_intr_s = (double)p->hook_interrupt_ns / 1e9;
    double jit_s = total_s - hook_block_s - hook_intr_s;

    fprintf(stderr, "Wall time in uc_emu_start():  %8.3f s  (%llu calls, %.1f us/call)\n",
            total_s, (unsigned long long)p->emu_start_count,
            p->emu_start_count ? (double)p->emu_start_ns / p->emu_start_count / 1e3 : 0);

    fprintf(stderr, "  hook_block() total:        %8.3f s  (%4.1f%%)  (%llu calls, %.1f us/call)\n",
            hook_block_s, total_s > 0 ? 100.0 * hook_block_s / total_s : 0,
            (unsigned long long)p->hook_block_count,
            p->hook_block_count ? (double)p->hook_block_ns / p->hook_block_count / 1e3 : 0);

    fprintf(stderr, "  hook_interrupt() total:    %8.3f s  (%4.1f%%)  (%llu EmulOps, %.1f us/op)\n",
            hook_intr_s, total_s > 0 ? 100.0 * hook_intr_s / total_s : 0,
            (unsigned long long)p->emulop_count,
            p->emulop_count ? (double)p->hook_interrupt_ns / p->emulop_count / 1e3 : 0);

    fprintf(stderr, "  JIT execution (estimated): %8.3f s  (%4.1f%%)\n",
            jit_s, total_s > 0 ? 100.0 * jit_s / total_s : 0);

    fprintf(stderr, "  Interrupts delivered:       %llu\n",
            (unsigned long long)p->interrupt_count);

    fprintf(stderr, "  Deferred reg updates:       %llu\n",
            (unsigned long long)p->deferred_update_count);

    fprintf(stderr, "  TB cache flushes:           %llu\n",
            (unsigned long long)p->flush_code_cache_count);

    /* TB find hit/miss stats from QEMU cpu-exec.c */
    extern uint64_t g_tb_find_count;
    extern uint64_t g_tb_miss_count;
    extern uint64_t g_tb_buffer_flush_count;
    if (g_tb_find_count > 0) {
        fprintf(stderr, "  tb_find() calls:            %llu\n",
                (unsigned long long)g_tb_find_count);
        fprintf(stderr, "    tb_gen_code (compile):     %llu (%.1f%%)\n",
                (unsigned long long)g_tb_miss_count,
                100.0 * g_tb_miss_count / g_tb_find_count);
        fprintf(stderr, "  Code buffer full flushes:   %llu\n",
                (unsigned long long)g_tb_buffer_flush_count);
    }

    fprintf(stderr, "  uc_emu_start() restarts:    %llu (%.1f/sec)\n",
            (unsigned long long)p->emu_start_count,
            total_s > 0 ? (double)p->emu_start_count / total_s : 0);

    fprintf(stderr, "========================================\n\n");
}

/* Print block statistics */
void unicorn_print_block_stats(UnicornCPU *cpu) {
    if (!cpu) return;
    fprintf(stderr, "Total blocks executed: %llu\n",
            (unsigned long long)cpu->block_stats.total_blocks);
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
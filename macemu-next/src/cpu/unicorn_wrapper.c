/**
 * Unicorn Engine Wrapper Implementation (Cleaned Version)
 *
 * Provides M68K emulation using Unicorn engine with A-line EmulOp support.
 * All obsolete MMIO transport code has been removed.
 */

#include "unicorn_wrapper.h"
#include "platform.h"
#include "cpu_trace.h"
#include "timer_interrupt.h"
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations for EmulOp interface */
struct M68kRegistersC {
    uint32_t d[8];  /* Data registers D0-D7 */
    uint32_t a[8];  /* Address registers A0-A7 */
    uint16_t sr;    /* Status register */
};
extern void EmulOp_C(uint16_t opcode, struct M68kRegistersC *r);

/* M68K interrupt trigger (from Unicorn's QEMU backend) */
extern void uc_m68k_trigger_interrupt(uc_engine *uc, int level, uint8_t vector);

/* Interrupt handling */
static volatile int g_pending_interrupt_level = 0;

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
};

/* Deferred SR update API */
void unicorn_defer_sr_update(void *unicorn_cpu, uint16_t new_sr) {
    UnicornCPU *cpu = (UnicornCPU *)unicorn_cpu;
    if (cpu) {
        cpu->has_deferred_sr_update = true;
        cpu->deferred_sr_value = new_sr;
    }
}

/**
 * Hook for block execution (UC_HOOK_BLOCK)
 * Called at the start of each translation block.
 * Used for interrupt checking and block statistics.
 */
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    /* Update block statistics */
    cpu->block_stats.total_blocks++;
    cpu->block_stats.total_instructions += size;

    if (size < cpu->block_stats.min_block_size) {
        cpu->block_stats.min_block_size = size;
    }
    if (size > cpu->block_stats.max_block_size) {
        cpu->block_stats.max_block_size = size;
    }

    cpu->block_stats.sum_block_sizes += size;

    /* Update histogram */
    if (size <= 100) {
        cpu->block_stats.block_size_histogram[size]++;
    } else {
        cpu->block_stats.block_size_histogram[100]++;
    }

    /* Check for pending interrupts */
    if (g_pending_interrupt_level > 0) {
        uint16_t sr;
        uc_reg_read(uc, UC_M68K_REG_SR, &sr);
        int current_ipl = (sr >> 8) & 7;

        if (g_pending_interrupt_level > current_ipl) {
            /* Timer interrupt is autovectored level 1 */
            uint8_t vector = (g_pending_interrupt_level == 1) ? 0x19 :
                           (0x18 + g_pending_interrupt_level);

            uc_m68k_trigger_interrupt(uc, g_pending_interrupt_level, vector);
            g_pending_interrupt_level = 0;
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

    /* Check for A-line exception (interrupt #10) */
    if (intno == 10) {
        uint32_t pc;
        uint16_t opcode;

        /* Get PC and read opcode */
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_mem_read(uc, pc, &opcode, sizeof(opcode));
        opcode = (opcode >> 8) | (opcode << 8);  /* Swap bytes for big-endian */

        /* Check if it's in our EmulOp range (0xAE00-0xAE3F) */
        if ((opcode & 0xFFC0) == 0xAE00) {
            /* Convert A-line opcode to legacy EmulOp format */
            uint16_t legacy_opcode = 0x7100 | (opcode & 0x3F);

            /* Call the platform EmulOp handler */
            if (g_platform.emulop_handler) {
                bool pc_advanced = g_platform.emulop_handler(legacy_opcode, false);

                /* Advance PC past the EmulOp if handler didn't */
                if (!pc_advanced) {
                    pc += 2;
                    uc_reg_write(uc, UC_M68K_REG_PC, &pc);
                }
            }
        }
    }
    /* Other exceptions are just acknowledged */
}

/**
 * Hook for invalid instructions (UC_HOOK_INSN_INVALID)
 * Handles legacy 0x71xx EmulOps and A-line/F-line traps.
 */
static bool hook_insn_invalid(uc_engine *uc, void *user_data) {
    uint32_t pc;
    uint16_t opcode;

    /* Get current PC and read opcode */
    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));
    opcode = (opcode >> 8) | (opcode << 8);  /* Swap for big-endian */

    /* Check if EmulOp (0x71xx) */
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

    /* Check for A-line (0xAxxx) or F-line (0xFxxx) traps */
    if ((opcode & 0xF000) == 0xA000) {
        if (g_platform.trap_handler) {
            g_platform.trap_handler(10, opcode, false);  /* 10 = A-line trap */
            return true;  /* Continue execution */
        }
    } else if ((opcode & 0xF000) == 0xF000) {
        if (g_platform.trap_handler) {
            g_platform.trap_handler(11, opcode, false);  /* 11 = F-line trap */
            return true;  /* Continue execution */
        }
    }

    return false;  /* Stop execution */
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
    uc_mode mode = (arch == UC_ARCH_M68K) ? UC_MODE_BIG_ENDIAN : UC_MODE_LITTLE_ENDIAN;
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

    /* Apply deferred SR update if needed */
    if (cpu->has_deferred_sr_update) {
        uc_reg_write(cpu->uc, UC_M68K_REG_SR, &cpu->deferred_sr_value);
        cpu->has_deferred_sr_update = false;
    }

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
        uc_reg_write(cpu->uc, UC_M68K_REG_A0 + reg, &val);
    }
}

uint16_t unicorn_get_sr(UnicornCPU *cpu) {
    uint16_t sr = 0;
    if (cpu && cpu->uc) {
        uc_reg_read(cpu->uc, UC_M68K_REG_SR, &sr);
    }
    return sr;
}

void unicorn_set_sr(UnicornCPU *cpu, uint16_t sr) {
    if (cpu && cpu->uc) {
        uc_reg_write(cpu->uc, UC_M68K_REG_SR, &sr);
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
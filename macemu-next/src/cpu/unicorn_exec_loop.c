/*
 * unicorn_exec_loop.c - QEMU-style execution loop for Unicorn
 *
 * Phase 2 Implementation: Wrap Unicorn execution in a control loop
 * that checks interrupts between small instruction batches.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>

// Forward declaration of UnicornCPU
typedef struct UnicornCPU UnicornCPU;

// Function declarations from unicorn_wrapper.h
extern void* unicorn_get_uc(UnicornCPU *cpu);
extern bool unicorn_poll_interrupts(UnicornCPU *cpu);
extern bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc);

// Forward declarations for external functions
extern bool poll_and_check_interrupts(UnicornCPU *cpu);
extern bool handle_invalid_insn(UnicornCPU *cpu, uint32_t pc);

// Include platform API
#include "../common/include/platform.h"

// Phase 3: Handle EmulOp with immediate register updates
static bool handle_emulop_immediate(UnicornCPU *cpu, uint16_t opcode);

// Helper: Check for backward branches that indicate loops
static bool detected_backward_branch(UnicornCPU *cpu) {
    uint32_t pc;
    uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);
    uc_reg_read(uc, UC_M68K_REG_PC, &pc);

    // Check if we're in known IRQ polling loop regions
    // Classic ROM: 0x402be2 - 0x402be6
    // 32-bit ROM: 0x4080a294 - 0x4080a298
    if ((pc >= 0x402be2 && pc <= 0x402be6) ||
        (pc >= 0x4080a294 && pc <= 0x4080a298)) {
        return true;  // Force interrupt check
    }

    // Also check for the patched locations
    // 0x2be4 (Classic) and 0xa296 (32-bit) in ROM space
    if ((pc >= 0x02002be4 && pc <= 0x02002bf0) ||
        (pc >= 0x0200a296 && pc <= 0x0200a2a0)) {
        return true;
    }

    return false;
}

// Calculate adaptive batch size based on PC location
static int calculate_batch_size(UnicornCPU *cpu, uint32_t pc) {
    // Very small batches for known hot loops and IRQ regions
    if ((pc >= 0x02002be0 && pc <= 0x02002c00) ||  // Classic ROM IRQ region
        (pc >= 0x0200a290 && pc <= 0x0200a2b0)) {   // 32-bit ROM IRQ region
        return 3;  // Tiny batch for IRQ polling
    }

    // Small batches for ROM code that might have loops
    if (pc >= 0x02000000 && pc <= 0x02100000) {
        return 20;  // ROM code
    }

    // Medium batches for RAM code
    if (pc < 0x02000000) {
        return 50;  // Application code in RAM
    }

    // Default medium batch
    return 30;
}

// Main QEMU-inspired execution loop with interrupt checking
int unicorn_execute_with_interrupts(UnicornCPU *cpu, int max_total_insns) {
    int total_executed = 0;
    uint32_t last_pc = 0;
    int stuck_count = 0;
    bool verbose = getenv("CPU_VERBOSE") != NULL;

    if (verbose) {
        fprintf(stderr, "[exec_loop] Starting execution loop for %d instructions\n", max_total_insns);
    }

    while (total_executed < max_total_insns) {
        uint32_t pc;
        uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);

        // Detect if we're stuck at the same PC (infinite loop)
        if (pc == last_pc) {
            stuck_count++;
            if (stuck_count > 10) {
                if (verbose) {
                    fprintf(stderr, "[exec_loop] Stuck at PC 0x%08x, forcing interrupt check\n", pc);
                }
                // Force interrupt check when stuck
                if (poll_and_check_interrupts(cpu)) {
                    stuck_count = 0;
                    continue;
                }
                // If no interrupt, try to advance anyway
                stuck_count = 0;
            }
        } else {
            stuck_count = 0;
            last_pc = pc;
        }

        // CRITICAL: Check interrupts BEFORE execution (QEMU pattern)
        // This is especially important for tight polling loops
        if (poll_and_check_interrupts(cpu)) {
            if (verbose) {
                fprintf(stderr, "[exec_loop] Interrupt delivered at PC 0x%08x\n", pc);
            }
            // Interrupt was delivered, restart loop
            continue;
        }

        // Calculate batch size based on current PC
        int batch_size = calculate_batch_size(cpu, pc);
        int to_execute = (max_total_insns - total_executed);
        if (to_execute > batch_size) {
            to_execute = batch_size;
        }

        if (verbose && (total_executed % 1000) == 0) {
            fprintf(stderr, "[exec_loop] Executing batch of %d at PC 0x%08x (total: %d)\n",
                    to_execute, pc, total_executed);
        }

        // Execute small batch
        uc_err err = uc_emu_start(uc, pc, 0, 0, to_execute);

        // Update count (even if there was an error, some instructions may have executed)
        // We'll get the actual count from Unicorn later if needed
        total_executed += to_execute;

        // Handle errors
        if (err == UC_ERR_INSN_INVALID) {
            uint16_t opcode;
            uc_mem_read(uc, pc, &opcode, 2);
            opcode = __builtin_bswap16(opcode);  // Fix endianness

            if (verbose) {
                fprintf(stderr, "[exec_loop] Invalid instruction 0x%04x at PC 0x%08x\n", opcode, pc);
            }

            // Check if it's an EmulOp that should be handled
            if ((opcode & 0xFF00) == 0x7100) {
                // Phase 3: Handle EmulOp with immediate register updates
                if (!handle_emulop_immediate(cpu, opcode)) {
                    fprintf(stderr, "[exec_loop] Failed to handle EmulOp 0x%04x at PC 0x%08x\n", opcode, pc);
                    return -1;
                }
                // EmulOp handled successfully, continue execution
            } else if (opcode == 0x4E73) {
                // Phase 4: Handle RTE (Return from Exception)
                extern bool handle_rte(UnicornCPU *cpu);
                if (!handle_rte(cpu)) {
                    fprintf(stderr, "[exec_loop] Failed to handle RTE at PC 0x%08x\n", pc);
                    return -1;
                }
                // RTE handled successfully, continue execution
            } else {
                // Real illegal instruction
                if (!handle_invalid_insn(cpu, pc)) {
                    fprintf(stderr, "[exec_loop] Unhandled illegal instruction 0x%04x at PC 0x%08x\n",
                            opcode, pc);
                    return -1;
                }
            }
        } else if (err != UC_ERR_OK && err != UC_ERR_FETCH_UNMAPPED) {
            // UC_ERR_FETCH_UNMAPPED happens when we hit our instruction limit
            if (err != UC_ERR_FETCH_UNMAPPED) {
                fprintf(stderr, "[exec_loop] Unicorn error %d at PC 0x%08x\n", err, pc);
                return -1;
            }
        }

        // Check if we hit a backward branch (force interrupt check)
        if (detected_backward_branch(cpu)) {
            if (verbose) {
                fprintf(stderr, "[exec_loop] Backward branch detected at PC 0x%08x, checking interrupts\n", pc);
            }
            // Check interrupts immediately after backward branches
            if (poll_and_check_interrupts(cpu)) {
                continue;
            }
        }

        // Also check interrupts periodically (every N instructions)
        if ((total_executed % 100) == 0) {
            if (poll_and_check_interrupts(cpu)) {
                if (verbose) {
                    fprintf(stderr, "[exec_loop] Periodic interrupt at %d instructions\n", total_executed);
                }
            }
        }
    }

    if (verbose) {
        fprintf(stderr, "[exec_loop] Execution loop completed, executed %d instructions\n", total_executed);
    }

    return total_executed;
}

// Helper function to check and deliver interrupts
bool poll_and_check_interrupts(UnicornCPU *cpu) {
    // Phase 4: Use proper M68K interrupt delivery
    extern bool check_and_deliver_interrupts(UnicornCPU *cpu);
    return check_and_deliver_interrupts(cpu);
}

// Helper function to handle invalid instructions
bool handle_invalid_insn(UnicornCPU *cpu, uint32_t pc) {
    // This will be implemented by linking with the existing handler
    // For now, return false to indicate unhandled
    // The actual implementation is in unicorn_wrapper.c
    extern bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc);
    return unicorn_handle_illegal(cpu, pc);
}

// Phase 3: Handle EmulOp with immediate register updates
static bool handle_emulop_immediate(UnicornCPU *cpu, uint16_t opcode) {
    uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);

    // Call the platform EmulOp handler
    if (!g_platform.emulop_handler) {
        return false;
    }

    bool verbose = getenv("EMULOP_VERBOSE") != NULL;
    if (verbose) {
        fprintf(stderr, "[handle_emulop_immediate] Processing EmulOp 0x%04x\n", opcode);
    }

    // Save register state before EmulOp
    uint32_t old_d0 = 0, old_a0 = 0, old_sr = 0;
    if (verbose) {
        if (g_platform.cpu_get_dreg) old_d0 = g_platform.cpu_get_dreg(0);
        if (g_platform.cpu_get_areg) old_a0 = g_platform.cpu_get_areg(0);
        if (g_platform.cpu_get_sr) old_sr = g_platform.cpu_get_sr();
    }

    // Call the EmulOp handler
    bool pc_advanced = g_platform.emulop_handler(opcode, false);

    // Phase 3: Apply register updates IMMEDIATELY
    // This replaces the deferred update mechanism

    // Update all D registers
    if (g_platform.cpu_get_dreg && g_platform.cpu_set_dreg) {
        for (int i = 0; i < 8; i++) {
            uint32_t dreg = g_platform.cpu_get_dreg(i);
            uc_reg_write(uc, UC_M68K_REG_D0 + i, &dreg);
        }
    }

    // Update all A registers
    if (g_platform.cpu_get_areg && g_platform.cpu_set_areg) {
        for (int i = 0; i < 8; i++) {
            uint32_t areg = g_platform.cpu_get_areg(i);
            uc_reg_write(uc, UC_M68K_REG_A0 + i, &areg);
        }
    }

    // Update SR
    if (g_platform.cpu_get_sr && g_platform.cpu_set_sr) {
        uint32_t sr = g_platform.cpu_get_sr();
        uc_reg_write(uc, UC_M68K_REG_SR, &sr);

        if (verbose && sr != old_sr) {
            fprintf(stderr, "[handle_emulop_immediate] SR changed: 0x%04x -> 0x%04x\n", old_sr, sr);
        }
    }

    // Advance PC if the handler didn't
    if (!pc_advanced) {
        uint32_t pc;
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        pc += 2;
        uc_reg_write(uc, UC_M68K_REG_PC, &pc);

        if (verbose) {
            fprintf(stderr, "[handle_emulop_immediate] Advanced PC to 0x%08x\n", pc);
        }
    }

    if (verbose) {
        if (g_platform.cpu_get_dreg && old_d0 != g_platform.cpu_get_dreg(0)) {
            fprintf(stderr, "[handle_emulop_immediate] D0 changed: 0x%08x -> 0x%08x\n",
                    old_d0, g_platform.cpu_get_dreg(0));
        }
    }

    return true;
}
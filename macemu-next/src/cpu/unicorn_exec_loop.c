/*
 * unicorn_exec_loop.c - Execution loop for Unicorn CPU backend
 *
 * Simple wrapper that calls uc_emu_start in a loop.
 * Timer polling and interrupt delivery are handled EXCLUSIVELY by hook_block
 * in unicorn_wrapper.c, using QEMU's native interrupt mechanism.
 *
 * DO NOT poll timers or deliver interrupts here - it conflicts with
 * QEMU's interrupt frame format (68020+ Format 2 vs manual Format 0).
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
extern bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc);

// Include platform API
#include "../common/include/platform.h"

// Main execution loop - just restart uc_emu_start after each stop
int unicorn_execute_with_interrupts(UnicornCPU *cpu, int max_total_insns) {
    int total_executed = 0;
    bool verbose = getenv("CPU_VERBOSE") != NULL;

    if (verbose) {
        fprintf(stderr, "[exec_loop] Starting execution loop for %d instructions\n", max_total_insns);
    }

    while (total_executed < max_total_insns) {
        uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);
        uint32_t pc;
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);

        if (verbose && (total_executed % 10000) == 0) {
            fprintf(stderr, "[exec_loop] Executing at PC 0x%08x (total: %d)\n",
                    pc, total_executed);
        }

        // Detect PC=0 (invalid - vector table area, not code)
        if (pc == 0 && total_executed > 0) {
            uint32_t d0=0, d1=0, d2=0, a0=0, a7=0, sr=0;
            uc_reg_read(uc, UC_M68K_REG_D0, &d0);
            uc_reg_read(uc, UC_M68K_REG_D1, &d1);
            uc_reg_read(uc, UC_M68K_REG_D2, &d2);
            uc_reg_read(uc, UC_M68K_REG_A0, &a0);
            uc_reg_read(uc, UC_M68K_REG_A7, &a7);
            uc_reg_read(uc, UC_M68K_REG_SR, &sr);
            uint8_t stk[32] = {0};
            uc_mem_read(uc, a7, stk, 32);
            fprintf(stderr, "[exec_loop] PC=0 detected! total_executed=%d\n"
                    "  D0=0x%08x D1=0x%08x D2=0x%08x A0=0x%08x A7=0x%08x SR=0x%04x\n"
                    "  stack: %02x%02x %02x%02x%02x%02x %02x%02x"
                    " | %02x%02x %02x%02x%02x%02x %02x%02x\n",
                    total_executed, d0, d1, d2, a0, a7, sr & 0xFFFF,
                    stk[0],stk[1],stk[2],stk[3],stk[4],stk[5],stk[6],stk[7],
                    stk[8],stk[9],stk[10],stk[11],stk[12],stk[13],stk[14],stk[15]);
            return -1;
        }

        // Execute continuously with count=0 (unlimited).
        // Timer polling and interrupt delivery are handled by hook_block.
        // uc_emu_stop() from hook_block (interrupt delivery) or
        // hook_interrupt (Execute68kTrap sentinel) causes return.
        //
        // IMPORTANT: Do NOT use instruction count limits (count > 0)!
        // QEMU's icount exit does NOT call cpu_restore_state, so cc_op
        // is not properly synced when a TB is interrupted mid-execution.
        uc_err err = uc_emu_start(uc, pc, 0xFFFFFFFF, 0, 0);

        // With count=0, we don't know exact instruction count.
        total_executed += 1000;

        // Handle errors
        if (err == UC_ERR_INSN_INVALID) {
            uc_reg_read(uc, UC_M68K_REG_PC, &pc);
            uint16_t opcode;
            uc_mem_read(uc, pc, &opcode, 2);
            opcode = __builtin_bswap16(opcode);

            if (verbose) {
                fprintf(stderr, "[exec_loop] Invalid instruction 0x%04x at PC 0x%08x\n", opcode, pc);
            }

            // Legacy 0x71xx EmulOps should have been converted to 0xAExx.
            // If we see one, it's a bug.
            if ((opcode & 0xFF00) == 0x7100) {
                fprintf(stderr, "[exec_loop] ERROR: Unconverted 0x71xx EmulOp 0x%04x at PC 0x%08x\n",
                        opcode, pc);
                return -1;
            }

            // Other illegal instructions
            if (!unicorn_handle_illegal(cpu, pc)) {
                fprintf(stderr, "[exec_loop] Unhandled illegal instruction 0x%04x at PC 0x%08x\n",
                        opcode, pc);
                return -1;
            }
        } else if (err != UC_ERR_OK) {
            // UC_ERR_OK: normal return from uc_emu_stop
            // Any other error is unexpected
            if (err != UC_ERR_FETCH_UNMAPPED) {
                fprintf(stderr, "[exec_loop] Unicorn error %d at PC 0x%08x: %s\n",
                        err, pc, uc_strerror(err));
                return -1;
            }
            // UC_ERR_FETCH_UNMAPPED can happen at loop boundaries, just restart
        }
    }

    if (verbose) {
        fprintf(stderr, "[exec_loop] Execution loop completed, executed %d instructions\n", total_executed);
    }

    return total_executed;
}

// Legacy stubs - these are no longer used but referenced by the build
bool poll_and_check_interrupts(UnicornCPU *cpu) {
    (void)cpu;
    return false;  // No-op: interrupts handled by hook_block
}

bool handle_invalid_insn(UnicornCPU *cpu, uint32_t pc) {
    extern bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc);
    return unicorn_handle_illegal(cpu, pc);
}

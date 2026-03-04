/*
 * unicorn_exec_loop.c - Execution loop for Unicorn CPU backend
 *
 * Calls uc_emu_start() in a loop, restarting after each uc_emu_stop().
 * Timer polling and interrupt delivery are handled EXCLUSIVELY by hook_block
 * in unicorn_wrapper.c, using QEMU's native interrupt mechanism.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>

typedef struct UnicornCPU UnicornCPU;

extern void* unicorn_get_uc(UnicornCPU *cpu);
extern bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc);

/*
 * Main execution loop.
 *
 * Each uc_emu_start() runs until uc_emu_stop() is called (from hook_block
 * for interrupt delivery, or hook_interrupt for Execute68kTrap sentinel).
 * We just restart from the new PC.
 *
 * The max_iterations parameter caps loop restarts, not instructions.
 * With count=0, Unicorn runs unlimited instructions per start.
 */
int unicorn_execute_with_interrupts(UnicornCPU *cpu, int max_iterations) {
    uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);
    int iterations = 0;

    while (iterations < max_iterations) {
        uint32_t pc;
        uc_reg_read(uc, UC_M68K_REG_PC, &pc);

        /* PC=0 means we jumped into the vector table — fatal */
        if (pc == 0 && iterations > 0) {
            fprintf(stderr, "[exec_loop] PC=0 detected after %d iterations\n", iterations);
            return -1;
        }

        /*
         * Execute with count=0 (unlimited instructions).
         * IMPORTANT: Do NOT use count > 0. QEMU's icount exit does NOT call
         * cpu_restore_state, so cc_op is not synced when a TB is interrupted.
         * NOTE: timeout > 0 also causes regressions with M68K (breaks boot),
         * so we use timeout=0 as well.
         */
        uc_err err = uc_emu_start(uc, pc, 0xFFFFFFFF, 0, 0);
        iterations++;

        if (err == UC_ERR_INSN_INVALID) {
            uc_reg_read(uc, UC_M68K_REG_PC, &pc);
            uint16_t opcode;
            uc_mem_read(uc, pc, &opcode, 2);
            opcode = __builtin_bswap16(opcode);

            if ((opcode & 0xFF00) == 0x7100) {
                fprintf(stderr, "[exec_loop] ERROR: Unconverted 0x71xx EmulOp 0x%04x at PC 0x%08x\n",
                        opcode, pc);
                return -1;
            }

            if (!unicorn_handle_illegal(cpu, pc)) {
                fprintf(stderr, "[exec_loop] Unhandled illegal instruction 0x%04x at PC 0x%08x\n",
                        opcode, pc);
                return -1;
            }
        } else if (err != UC_ERR_OK && err != UC_ERR_FETCH_UNMAPPED) {
            fprintf(stderr, "[exec_loop] Unicorn error %d at PC 0x%08x: %s\n",
                    err, pc, uc_strerror(err));
            return -1;
        }
    }

    return iterations;
}

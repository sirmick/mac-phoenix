/*
 * mmio_emulop_transport.c - MMIO transport implementation for EmulOps
 *
 * This file contains the MMIO handler that bridges between memory-mapped I/O
 * and the existing EmulOp infrastructure. It's a separate file to make it
 * easy to integrate into unicorn_wrapper.c
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mmio_transport.h"

// Forward declarations - these exist elsewhere in the codebase
struct M68kRegisters;
extern void EmulOp(uint16_t opcode, struct M68kRegisters *r);

/*
 * MMIO handler for EmulOp transport
 * This should be integrated into unicorn_wrapper.c
 */

// Add this to UnicornCPU struct:
// uc_hook mmio_emulop_hook;  // UC_HOOK_MEM_WRITE for MMIO EmulOp transport

// This function should be added to unicorn_wrapper.c
static void mmio_emulop_handler(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user_data) {
    UnicornCPU *cpu = (UnicornCPU *)user_data;

    // Only handle writes
    if (type != UC_MEM_WRITE) {
        return;
    }

    // Check if this is in the MMIO EmulOp region
    if (!IS_MMIO_EMULOP(address)) {
        return;  // Not our region
    }

    // Convert MMIO address to EmulOp opcode
    uint16_t opcode = MMIO_TO_EMULOP(address);

    // Debug logging
    static int mmio_count = 0;
    if (++mmio_count <= 20) {  // Log first 20 for debugging
        fprintf(stderr, "[MMIO EmulOp] Triggered 0x%04x via write to 0x%08lx (value=0x%lx)\n",
                opcode, address, value);
    }

    // Build M68kRegisters from current Unicorn state
    struct M68kRegisters regs;
    memset(&regs, 0, sizeof(regs));

    // Read data registers
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
    }

    // Read address registers
    for (int i = 0; i < 8; i++) {
        uc_reg_read(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }

    // Read status register
    uc_reg_read(uc, UC_M68K_REG_SR, &regs.sr);

    // Call the existing EmulOp handler with the giant switch
    // This reuses ALL the existing EmulOp code!
    EmulOp(opcode, &regs);

    // Write back any register changes
    for (int i = 0; i < 8; i++) {
        uc_reg_write(uc, UC_M68K_REG_D0 + i, &regs.d[i]);
    }

    for (int i = 0; i < 8; i++) {
        uc_reg_write(uc, UC_M68K_REG_A0 + i, &regs.a[i]);
    }

    // Special handling for SR - may need deferred update
    uint16_t old_sr;
    uc_reg_read(uc, UC_M68K_REG_SR, &old_sr);
    if (regs.sr != old_sr) {
        // If SR changed, update it
        // Note: This might need special handling in hook context
        uc_reg_write(uc, UC_M68K_REG_SR, &regs.sr);
    }
}

/*
 * Setup function to register MMIO region
 * Add this to unicorn_create() in unicorn_wrapper.c, after uc_open()
 */
static bool setup_mmio_emulop_transport(UnicornCPU *cpu) {
    uc_err err;

    // First, map the MMIO region as read/write memory
    // This makes it accessible but we'll hook accesses to it
    fprintf(stderr, "[UNICORN] Mapping MMIO EmulOp region at 0x%08X-0x%08X\n",
            MMIO_EMULOP_BASE, MMIO_EMULOP_BASE + MMIO_EMULOP_SIZE - 1);

    err = uc_mem_map(cpu->uc, MMIO_EMULOP_BASE, MMIO_EMULOP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to map MMIO EmulOp region: %s\n", uc_strerror(err));
        return false;
    }

    // Now hook memory writes to this region
    fprintf(stderr, "[UNICORN] Installing MMIO EmulOp handler\n");

    err = uc_hook_add(cpu->uc, &cpu->mmio_emulop_hook,
                     UC_HOOK_MEM_WRITE,
                     mmio_emulop_handler,
                     cpu,  // user_data
                     MMIO_EMULOP_BASE,
                     MMIO_EMULOP_BASE + MMIO_EMULOP_SIZE - 1);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Failed to hook MMIO EmulOp region: %s\n", uc_strerror(err));
        return false;
    }

    fprintf(stderr, "[UNICORN] MMIO EmulOp transport ready\n");
    return true;
}

/*
 * Cleanup function
 * Add this to unicorn_destroy() in unicorn_wrapper.c
 */
static void cleanup_mmio_emulop_transport(UnicornCPU *cpu) {
    if (cpu->mmio_emulop_hook) {
        uc_hook_del(cpu->uc, cpu->mmio_emulop_hook);
        cpu->mmio_emulop_hook = 0;
    }
    // Memory will be unmapped when uc_close() is called
}

/*
 * Code to REMOVE from unicorn_wrapper.c:
 *
 * 1. In hook_block(), remove:
 *    if (opcode >= 0x7100 && opcode < 0x7140) {
 *        uc_emu_stop(uc);
 *        cpu->trap_ctx.in_emulop = true;
 *        return;
 *    }
 *
 * 2. In unicorn_execute_n(), remove all code checking for:
 *    (opcode & 0xFF00) == 0x7100
 *
 * 3. In hook_insn_invalid(), remove EmulOp handling for 0x71xx range
 *
 * 4. Remove the old trap_mem_fetch_handler if it's only used for EmulOps
 */
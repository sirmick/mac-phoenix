/**
 * Test which undefined opcodes trigger UC_HOOK_INSN_INVALID properly
 */

#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ADDRESS 0x1000

// Test various undefined opcode ranges
static const uint16_t test_opcodes[] = {
    // Known illegal
    0x4AFC,  // ILLEGAL instruction

    // F-line range
    0xF000,  // F-line trap
    0xF100,  // F-line trap

    // A-line range
    0xA000,  // A-line trap
    0xAE00,  // Our EmulOp attempt

    // Potentially undefined ranges
    0x06C0,  // Listed as undef in translate.c
    0x02C0,  // Listed as undef
    0x04C0,  // Listed as undef

    // 0x7Fxx range (above MOVEQ)
    0x7F00,
    0x7F40,
    0x7FFF,

    // High ranges
    0x0FFF,
    0x1FFF,
    0x2FFF,
    0x3FFF,
    0x5FFF,

    0  // End marker
};

static int hook_count = 0;
static uint16_t hooked_opcodes[100];

static bool hook_invalid_insn(uc_engine *uc, void *user_data)
{
    uint32_t pc;
    uint16_t opcode;

    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));

    // M68K is big-endian
    opcode = (opcode >> 8) | (opcode << 8);

    hooked_opcodes[hook_count++] = opcode;

    // Skip this instruction
    pc += 2;
    uc_reg_write(uc, UC_M68K_REG_PC, &pc);

    return true;  // Handled
}

int main()
{
    uc_engine *uc;
    uc_err err;
    uc_hook hook;

    printf("Testing undefined opcodes for UC_HOOK_INSN_INVALID\n");
    printf("===================================================\n\n");

    // Initialize Unicorn
    err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);
    if (err != UC_ERR_OK) {
        printf("Failed to initialize Unicorn: %s\n", uc_strerror(err));
        return 1;
    }

    // Map memory
    uc_mem_map(uc, TEST_ADDRESS, 0x1000, UC_PROT_ALL);

    // Add hook for invalid instructions
    uc_hook_add(uc, &hook, UC_HOOK_INSN_INVALID,
                (void*)hook_invalid_insn, NULL, 1, 0);

    // Test each opcode
    for (int i = 0; test_opcodes[i] != 0; i++) {
        uint16_t opcode = test_opcodes[i];
        uint8_t code[4];

        // Write opcode (big-endian)
        code[0] = opcode >> 8;
        code[1] = opcode & 0xFF;
        code[2] = 0x4E;  // NOP
        code[3] = 0x71;

        uc_mem_write(uc, TEST_ADDRESS, code, sizeof(code));

        // Reset hook count
        int prev_count = hook_count;

        // Try to execute
        err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 2, 0, 1);

        // Check result
        if (hook_count > prev_count) {
            printf("0x%04X: ✓ Hook called (handled by hook)\n", opcode);
        } else if (err == UC_ERR_INSN_INVALID) {
            printf("0x%04X: ✗ UC_ERR_INSN_INVALID (hook NOT called)\n", opcode);
        } else if (err == UC_ERR_EXCEPTION) {
            printf("0x%04X: ✗ UC_ERR_EXCEPTION (internal exception)\n", opcode);
        } else if (err == UC_ERR_OK) {
            printf("0x%04X: ? Executed successfully (valid instruction?)\n", opcode);
        } else {
            printf("0x%04X: ? Error: %s\n", opcode, uc_strerror(err));
        }
    }

    printf("\n===================================================\n");
    printf("Summary: %d opcodes triggered the hook\n", hook_count);
    if (hook_count > 0) {
        printf("Hooked opcodes:");
        for (int i = 0; i < hook_count; i++) {
            if (i % 8 == 0) printf("\n  ");
            printf("0x%04X ", hooked_opcodes[i]);
        }
        printf("\n");
    }
    printf("===================================================\n");

    // Cleanup
    uc_close(uc);
    return 0;
}
/**
 * Test A-line EmulOp handler (0xAE00-0xAE3F)
 *
 * This test verifies that:
 * 1. 0xAE00-0xAE3F opcodes trigger our custom handler
 * 2. Other A-line opcodes (e.g., 0xA000) still trigger normal A-line exception
 * 3. The handler can distinguish between different EmulOp numbers
 */

#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ADDRESS 0x1000

// EmulOp opcodes using A-line encoding
#define EMULOP_TEST_0   0xAE00  // EmulOp #0
#define EMULOP_TEST_10  0xAE0A  // EmulOp #10
#define EMULOP_TEST_63  0xAE3F  // EmulOp #63 (last valid)
#define ALINE_INVALID   0xAE40  // Outside EmulOp range
#define ALINE_GENERIC   0xA000  // Generic A-line trap

// Track which opcodes were executed
static int emulop_count = 0;
static uint16_t last_opcode = 0;
static bool got_exception = false;

// Hook for invalid instructions (including A-line)
static bool hook_invalid_insn(uc_engine *uc, void *user_data)
{
    uint32_t pc;
    uint16_t opcode;

    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));

    // M68K is big-endian
    opcode = (opcode >> 8) | (opcode << 8);

    printf("Invalid instruction hook: PC=0x%04x, opcode=0x%04x\n", pc, opcode);

    // Check if it's in our EmulOp range
    if ((opcode & 0xFFC0) == 0xAE00) {
        uint16_t emulop_num = opcode & 0x3F;
        printf("  -> EmulOp #%d detected!\n", emulop_num);
        emulop_count++;
        last_opcode = opcode;

        // Advance PC past the instruction
        pc += 2;
        uc_reg_write(uc, UC_M68K_REG_PC, &pc);
        return true; // Handled
    }

    printf("  -> Not an EmulOp, regular A-line trap\n");
    got_exception = true;
    return false; // Let it raise exception
}

int main()
{
    uc_engine *uc;
    uc_err err;
    uc_hook hook;

    printf("Testing A-line EmulOp handler (0xAE00-0xAE3F)\n");
    printf("==============================================\n\n");

    // Initialize Unicorn in M68K mode
    err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);
    if (err != UC_ERR_OK) {
        printf("Failed to initialize Unicorn: %s\n", uc_strerror(err));
        return 1;
    }

    // Map memory for code
    uc_mem_map(uc, TEST_ADDRESS, 0x1000, UC_PROT_ALL);

    // Add hook for invalid instructions (including A-line)
    uc_hook_add(uc, &hook, UC_HOOK_INSN_INVALID,
                (void*)hook_invalid_insn, NULL, 1, 0);

    // Test 1: EmulOp #0 (0xAE00)
    printf("Test 1: EmulOp #0 (0xAE00)\n");
    emulop_count = 0;
    last_opcode = 0;
    got_exception = false;

    uint8_t code1[] = { 0xAE, 0x00, 0x4E, 0x71 }; // AE00, NOP
    uc_mem_write(uc, TEST_ADDRESS, code1, sizeof(code1));

    err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 4, 0, 0);
    if (err == UC_ERR_OK) {
        printf("  ✓ Executed successfully\n");
        printf("  EmulOps executed: %d, last opcode: 0x%04x\n", emulop_count, last_opcode);
    } else {
        printf("  ✗ Error: %s\n", uc_strerror(err));
    }
    printf("\n");

    // Test 2: EmulOp #10 (0xAE0A)
    printf("Test 2: EmulOp #10 (0xAE0A)\n");
    emulop_count = 0;
    last_opcode = 0;
    got_exception = false;

    uint8_t code2[] = { 0xAE, 0x0A, 0x4E, 0x71 }; // AE0A, NOP
    uc_mem_write(uc, TEST_ADDRESS, code2, sizeof(code2));

    err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 4, 0, 0);
    if (err == UC_ERR_OK) {
        printf("  ✓ Executed successfully\n");
        printf("  EmulOps executed: %d, last opcode: 0x%04x\n", emulop_count, last_opcode);
    } else {
        printf("  ✗ Error: %s\n", uc_strerror(err));
    }
    printf("\n");

    // Test 3: EmulOp #63 (0xAE3F - last valid)
    printf("Test 3: EmulOp #63 (0xAE3F - last valid)\n");
    emulop_count = 0;
    last_opcode = 0;
    got_exception = false;

    uint8_t code3[] = { 0xAE, 0x3F, 0x4E, 0x71 }; // AE3F, NOP
    uc_mem_write(uc, TEST_ADDRESS, code3, sizeof(code3));

    err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 4, 0, 0);
    if (err == UC_ERR_OK) {
        printf("  ✓ Executed successfully\n");
        printf("  EmulOps executed: %d, last opcode: 0x%04x\n", emulop_count, last_opcode);
    } else {
        printf("  ✗ Error: %s\n", uc_strerror(err));
    }
    printf("\n");

    // Test 4: Invalid A-line outside EmulOp range (0xAE40)
    printf("Test 4: A-line outside EmulOp range (0xAE40)\n");
    emulop_count = 0;
    last_opcode = 0;
    got_exception = false;

    uint8_t code4[] = { 0xAE, 0x40, 0x4E, 0x71 }; // AE40, NOP
    uc_mem_write(uc, TEST_ADDRESS, code4, sizeof(code4));

    err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 4, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        printf("  ✓ Correctly raised exception\n");
        printf("  Got exception: %s\n", got_exception ? "yes" : "no");
    } else {
        printf("  ✗ Should have raised exception but got: %s\n", uc_strerror(err));
    }
    printf("\n");

    // Test 5: Generic A-line (0xA000)
    printf("Test 5: Generic A-line trap (0xA000)\n");
    emulop_count = 0;
    last_opcode = 0;
    got_exception = false;

    uint8_t code5[] = { 0xA0, 0x00, 0x4E, 0x71 }; // A000, NOP
    uc_mem_write(uc, TEST_ADDRESS, code5, sizeof(code5));

    err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 4, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        printf("  ✓ Correctly raised exception\n");
        printf("  Got exception: %s\n", got_exception ? "yes" : "no");
    } else {
        printf("  ✗ Should have raised exception but got: %s\n", uc_strerror(err));
    }
    printf("\n");

    // Summary
    printf("Summary\n");
    printf("=======\n");
    printf("Total EmulOps executed: %d\n", emulop_count);
    printf("All tests passed: %s\n", emulop_count == 3 ? "YES" : "NO");

    // Clean up
    uc_close(uc);
    return emulop_count == 3 ? 0 : 1;
}
/*
 * test_aline_simple.c - Simplified test for A-line EmulOp handling
 */

#include <unicorn/unicorn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define TEST_ADDRESS 0x1000
#define CODE_SIZE    0x1000  // 4KB minimum

static int hook_called = 0;

// Hook for interrupts
static void hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    hook_called++;
    printf("INTR Hook called: intno=%d\n", intno);

    if (intno == 10) {  // A-line exception
        uint32_t pc;
        uint16_t opcode;

        uc_reg_read(uc, UC_M68K_REG_PC, &pc);
        uc_mem_read(uc, pc, &opcode, sizeof(opcode));
        opcode = (opcode >> 8) | (opcode << 8);

        printf("  A-line at PC=0x%08x, opcode=0x%04x\n", pc, opcode);

        // Check if it's our EmulOp range
        if ((opcode & 0xFFC0) == 0xAE00) {
            uint16_t emulop_num = opcode & 0x3F;
            printf("  -> EmulOp #%d detected!\n", emulop_num);

            // Advance PC
            pc += 2;
            uc_reg_write(uc, UC_M68K_REG_PC, &pc);
        }
    }
}

int main()
{
    uc_engine *uc;
    uc_err err;
    uc_hook hook;

    printf("Simple A-Line EmulOp Test\n");
    printf("=========================\n\n");

    // Initialize Unicorn
    err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);
    if (err != UC_ERR_OK) {
        printf("Failed to initialize Unicorn: %s\n", uc_strerror(err));
        return 1;
    }

    // Set CPU model
    uc_ctl_set_cpu_model(uc, UC_CPU_M68K_M68040);

    // Map memory
    err = uc_mem_map(uc, TEST_ADDRESS, CODE_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        printf("Failed to map memory: %s\n", uc_strerror(err));
        uc_close(uc);
        return 1;
    }

    // Simple test code: single AE10 instruction followed by ILLEGAL
    uint8_t code[] = {
        0xAE, 0x10,  // A-line EmulOp #16
        0x4A, 0xFC   // ILLEGAL (to stop)
    };

    uc_mem_write(uc, TEST_ADDRESS, code, sizeof(code));

    // Add interrupt hook
    err = uc_hook_add(uc, &hook, UC_HOOK_INTR, hook_intr, NULL, 1, 0);
    if (err != UC_ERR_OK) {
        printf("Failed to add hook: %s\n", uc_strerror(err));
        uc_close(uc);
        return 1;
    }

    // Execute
    printf("Executing code at 0x%04x...\n", TEST_ADDRESS);
    err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + sizeof(code), 0, 2);

    printf("\nExecution result: %s\n", uc_strerror(err));
    printf("Hook called %d time(s)\n", hook_called);

    if (hook_called > 0) {
        printf("\n✅ Success! A-line EmulOps trigger interrupt hook!\n");
    } else {
        printf("\n❌ Failed! Hook not called\n");
    }

    uc_close(uc);
    return 0;
}
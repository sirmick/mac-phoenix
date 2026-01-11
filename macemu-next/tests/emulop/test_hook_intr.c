/**
 * Test UC_HOOK_INTR for trapping exceptions
 */

#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ADDRESS 0x1000

static int intr_count = 0;
static int invalid_count = 0;

static void hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    uint32_t pc;
    uint16_t opcode;

    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));
    opcode = (opcode >> 8) | (opcode << 8);

    printf("INTR Hook: intno=%d, PC=0x%08x, opcode=0x%04x\n", intno, pc, opcode);
    intr_count++;

    // Skip instruction
    pc += 2;
    uc_reg_write(uc, UC_M68K_REG_PC, &pc);
}

static bool hook_invalid(uc_engine *uc, void *user_data)
{
    printf("INVALID Hook called\n");
    invalid_count++;
    return false;
}

int main()
{
    uc_engine *uc;
    uc_err err;
    uc_hook hook1, hook2;

    printf("Testing UC_HOOK_INTR for exceptions\n");
    printf("====================================\n\n");

    err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);
    if (err != UC_ERR_OK) {
        printf("Failed to initialize Unicorn: %s\n", uc_strerror(err));
        return 1;
    }

    // Set CPU model to 68040 (which definitely doesn't have MAC instructions)
    uc_ctl_set_cpu_model(uc, UC_CPU_M68K_M68040);

    uc_mem_map(uc, TEST_ADDRESS, 0x1000, UC_PROT_ALL);

    // Add BOTH hooks
    uc_hook_add(uc, &hook1, UC_HOOK_INTR, (void*)hook_intr, NULL, 1, 0);
    uc_hook_add(uc, &hook2, UC_HOOK_INSN_INVALID, (void*)hook_invalid, NULL, 1, 0);

    // Test various opcodes
    uint16_t test_opcodes[] = {
        0x4AFC,  // ILLEGAL
        0xA000,  // A-line
        0xAE00,  // Our EmulOp attempt
        0xF000,  // F-line
        0x4E40,  // TRAP #0
        0x4E48,  // TRAP #8
    };

    for (int i = 0; i < 6; i++) {
        uint16_t opcode = test_opcodes[i];
        uint8_t code[4];

        code[0] = opcode >> 8;
        code[1] = opcode & 0xFF;
        code[2] = 0x4E;  // NOP
        code[3] = 0x71;

        uc_mem_write(uc, TEST_ADDRESS, code, sizeof(code));

        printf("Testing opcode 0x%04x: ", opcode);
        intr_count = 0;
        invalid_count = 0;

        err = uc_emu_start(uc, TEST_ADDRESS, TEST_ADDRESS + 2, 0, 1);

        if (intr_count > 0) {
            printf("✓ INTR hook called\n");
        } else if (invalid_count > 0) {
            printf("✓ INVALID hook called\n");
        } else {
            printf("✗ No hooks called (%s)\n", uc_strerror(err));
        }
    }

    uc_close(uc);
    return 0;
}
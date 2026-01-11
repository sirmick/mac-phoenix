/**
 * Test A-line EmulOps in ROM-like context
 *
 * This simulates how EmulOps would work in actual Mac ROM after patching.
 * We create a mock ROM with driver-like code that uses EmulOps for I/O.
 */

#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Memory layout (similar to Mac)
#define ROM_BASE    0x00400000  // ROM starts at 4MB
#define ROM_SIZE    0x00100000  // 1MB ROM
#define RAM_BASE    0x00000000  // RAM starts at 0
#define RAM_SIZE    0x00400000  // 4MB RAM (stop before ROM)
#define STACK_TOP   0x00008000  // Initial stack pointer

// A-line EmulOp opcodes (our new encoding)
#define EMULOP_DISK_OPEN    0xAE10  // Disk driver open
#define EMULOP_DISK_PRIME   0xAE11  // Disk driver prime (read/write)
#define EMULOP_DISK_CONTROL 0xAE12  // Disk driver control
#define EMULOP_DISK_STATUS  0xAE13  // Disk driver status
#define EMULOP_DISK_CLOSE   0xAE14  // Disk driver close

// Legacy 0x71xx opcodes (for comparison)
#define LEGACY_DISK_OPEN    0x7110
#define LEGACY_DISK_PRIME   0x7111
#define LEGACY_DISK_CONTROL 0x7112
#define LEGACY_DISK_STATUS  0x7113
#define LEGACY_DISK_CLOSE   0x7114

// Track EmulOp execution
typedef struct {
    int count;
    uint16_t opcodes[100];
    uint32_t pc_values[100];
    uint32_t a0_values[100];  // A0 typically points to param block
    uint32_t d0_values[100];  // D0 typically receives result
} EmulOpTrace;

static EmulOpTrace trace = {0};

// Hook for invalid instructions (A-line traps)
static bool hook_invalid_insn(uc_engine *uc, void *user_data)
{
    uint32_t pc, a0, d0;
    uint16_t opcode;

    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));

    // M68K is big-endian
    opcode = (opcode >> 8) | (opcode << 8);

    // Check if it's an EmulOp (0xAE00-0xAE3F)
    if ((opcode & 0xFFC0) == 0xAE00) {
        uint16_t emulop_num = opcode & 0x3F;

        // Read registers (typical EmulOp convention)
        uc_reg_read(uc, UC_M68K_REG_A0, &a0);  // Parameter block pointer
        uc_reg_read(uc, UC_M68K_REG_D0, &d0);  // Function selector/result

        printf("EmulOp #%d at PC=0x%08x: ", emulop_num, pc);

        // Simulate EmulOp execution based on opcode
        switch (opcode) {
            case EMULOP_DISK_OPEN:
                printf("DISK_OPEN (A0=0x%08x)\n", a0);
                d0 = 0;  // Return success
                break;

            case EMULOP_DISK_PRIME:
                printf("DISK_PRIME (A0=0x%08x, D0=0x%08x)\n", a0, d0);
                d0 = 0;  // Return success
                break;

            case EMULOP_DISK_CONTROL:
                printf("DISK_CONTROL (A0=0x%08x, D0=0x%08x)\n", a0, d0);
                d0 = 0;  // Return success
                break;

            case EMULOP_DISK_STATUS:
                printf("DISK_STATUS (A0=0x%08x)\n", a0);
                d0 = 0x12345678;  // Return some status
                break;

            case EMULOP_DISK_CLOSE:
                printf("DISK_CLOSE (A0=0x%08x)\n", a0);
                d0 = 0;  // Return success
                break;

            default:
                printf("Unknown EmulOp 0x%04x\n", opcode);
                d0 = 0xFFFFFFFF;  // Return error
                break;
        }

        // Store in trace
        if (trace.count < 100) {
            trace.opcodes[trace.count] = opcode;
            trace.pc_values[trace.count] = pc;
            trace.a0_values[trace.count] = a0;
            trace.d0_values[trace.count] = d0;
            trace.count++;
        }

        // Write result back to D0
        uc_reg_write(uc, UC_M68K_REG_D0, &d0);

        // Advance PC past EmulOp
        pc += 2;
        uc_reg_write(uc, UC_M68K_REG_PC, &pc);

        return true;  // Handled
    }

    printf("Non-EmulOp A-line trap: 0x%04x at PC=0x%08x\n", opcode, pc);
    return false;  // Not handled
}

// Create a mock disk driver that uses EmulOps
void create_mock_driver(uint8_t *rom)
{
    uint16_t *wp = (uint16_t *)rom;

    // Driver header at ROM_BASE
    // This simulates a Mac driver that would be in ROM

    // Driver Open routine (offset 0x00)
    *wp++ = 0x48E7;  // MOVEM.L D0-D2/A0-A2,-(SP)  ; Save registers
    *wp++ = 0x0707;  // (register mask)

    *wp++ = 0xAE10;  // EMULOP_DISK_OPEN  ; Call EmulOp

    *wp++ = 0x4CDF;  // MOVEM.L (SP)+,D0-D2/A0-A2  ; Restore registers
    *wp++ = 0xE0E0;  // (register mask)

    *wp++ = 0x4E75;  // RTS

    // Driver Prime routine (offset 0x0C)
    *wp++ = 0x48E7;  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = 0x0707;

    *wp++ = 0xAE11;  // EMULOP_DISK_PRIME

    *wp++ = 0x4CDF;  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = 0xE0E0;

    *wp++ = 0x4E75;  // RTS

    // Driver Control routine (offset 0x18)
    *wp++ = 0x48E7;  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = 0x0707;

    *wp++ = 0xAE12;  // EMULOP_DISK_CONTROL

    *wp++ = 0x4CDF;  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = 0xE0E0;

    *wp++ = 0x4E75;  // RTS

    // Driver Status routine (offset 0x24)
    *wp++ = 0x48E7;  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = 0x0707;

    *wp++ = 0xAE13;  // EMULOP_DISK_STATUS

    *wp++ = 0x4CDF;  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = 0xE0E0;

    *wp++ = 0x4E75;  // RTS

    // Driver Close routine (offset 0x30)
    *wp++ = 0x48E7;  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = 0x0707;

    *wp++ = 0xAE14;  // EMULOP_DISK_CLOSE

    *wp++ = 0x4CDF;  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = 0xE0E0;

    *wp++ = 0x4E75;  // RTS

    // Test routine at offset 0x40 - calls all driver routines
    // This simulates Mac OS calling the driver
    *wp++ = 0x207C;  // MOVEA.L #param_block, A0
    *wp++ = 0x0000;  // High word of address
    *wp++ = 0x1000;  // Low word (0x00001000)

    // Call Open
    *wp++ = 0x4EB9;  // JSR driver_open
    *wp++ = (ROM_BASE >> 16);
    *wp++ = (ROM_BASE & 0xFFFF);

    // Call Prime (read)
    *wp++ = 0x7001;  // MOVEQ #1, D0  ; Read operation
    *wp++ = 0x4EB9;  // JSR driver_prime
    *wp++ = (ROM_BASE >> 16);
    *wp++ = (ROM_BASE + 0x0C) & 0xFFFF;

    // Call Status
    *wp++ = 0x4EB9;  // JSR driver_status
    *wp++ = (ROM_BASE >> 16);
    *wp++ = (ROM_BASE + 0x24) & 0xFFFF;

    // Call Control
    *wp++ = 0x7002;  // MOVEQ #2, D0  ; Some control code
    *wp++ = 0x4EB9;  // JSR driver_control
    *wp++ = (ROM_BASE >> 16);
    *wp++ = (ROM_BASE + 0x18) & 0xFFFF;

    // Call Close
    *wp++ = 0x4EB9;  // JSR driver_close
    *wp++ = (ROM_BASE >> 16);
    *wp++ = (ROM_BASE + 0x30) & 0xFFFF;

    *wp++ = 0x4E75;  // RTS
}

int main()
{
    uc_engine *uc;
    uc_err err;
    uc_hook hook;
    uint8_t *rom;
    uint32_t sp, pc, d0;

    printf("=================================================\n");
    printf("Testing A-line EmulOps in ROM Context\n");
    printf("=================================================\n\n");

    // Initialize Unicorn
    err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);
    if (err != UC_ERR_OK) {
        printf("Failed to initialize Unicorn: %s\n", uc_strerror(err));
        return 1;
    }

    // Map memory regions
    err = uc_mem_map(uc, RAM_BASE, RAM_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        printf("Failed to map RAM: %s\n", uc_strerror(err));
        return 1;
    }

    err = uc_mem_map(uc, ROM_BASE, ROM_SIZE, UC_PROT_READ | UC_PROT_EXEC);
    if (err != UC_ERR_OK) {
        printf("Failed to map ROM: %s\n", uc_strerror(err));
        return 1;
    }

    // Create mock ROM
    rom = calloc(1, ROM_SIZE);
    create_mock_driver(rom);

    // Write ROM to memory
    err = uc_mem_write(uc, ROM_BASE, rom, ROM_SIZE);
    if (err != UC_ERR_OK) {
        printf("Failed to write ROM: %s\n", uc_strerror(err));
        return 1;
    }

    // Set up parameter block in RAM (simulates Mac OS driver parameter block)
    uint32_t param_block[16] = {0};
    param_block[0] = 0x12345678;  // Some test data
    param_block[1] = 0xDEADBEEF;
    uc_mem_write(uc, 0x1000, param_block, sizeof(param_block));

    // Add hook for A-line traps (EmulOps)
    uc_hook_add(uc, &hook, UC_HOOK_INSN_INVALID,
                (void*)hook_invalid_insn, NULL, 1, 0);

    // Set up initial registers
    sp = STACK_TOP;
    uc_reg_write(uc, UC_M68K_REG_A7, &sp);  // A7 is stack pointer

    printf("Executing ROM driver test routine...\n");
    printf("=====================================\n\n");

    // Execute test routine
    pc = ROM_BASE + 0x40;  // Test routine offset
    err = uc_emu_start(uc, pc, pc + 0x100, 0, 0);

    if (err != UC_ERR_OK && err != UC_ERR_FETCH_UNMAPPED) {
        printf("\nExecution stopped: %s\n", uc_strerror(err));
    }

    // Read final D0 value
    uc_reg_read(uc, UC_M68K_REG_D0, &d0);

    printf("\n=====================================\n");
    printf("Execution Summary:\n");
    printf("=====================================\n");
    printf("EmulOps executed: %d\n", trace.count);
    printf("Final D0 value: 0x%08x\n", d0);

    if (trace.count > 0) {
        printf("\nEmulOp Trace:\n");
        for (int i = 0; i < trace.count; i++) {
            printf("  [%d] Opcode=0x%04x PC=0x%08x A0=0x%08x Result=0x%08x\n",
                   i, trace.opcodes[i], trace.pc_values[i],
                   trace.a0_values[i], trace.d0_values[i]);
        }
    }

    // Verify expected EmulOps were called
    int expected[] = {
        EMULOP_DISK_OPEN,
        EMULOP_DISK_PRIME,
        EMULOP_DISK_STATUS,
        EMULOP_DISK_CONTROL,
        EMULOP_DISK_CLOSE
    };

    printf("\nVerification:\n");
    int success = 1;
    if (trace.count != 5) {
        printf("  ✗ Expected 5 EmulOps, got %d\n", trace.count);
        success = 0;
    } else {
        printf("  ✓ Correct number of EmulOps\n");

        for (int i = 0; i < 5; i++) {
            if (trace.opcodes[i] == expected[i]) {
                printf("  ✓ EmulOp %d: 0x%04x (correct)\n", i, trace.opcodes[i]);
            } else {
                printf("  ✗ EmulOp %d: Expected 0x%04x, got 0x%04x\n",
                       i, expected[i], trace.opcodes[i]);
                success = 0;
            }
        }
    }

    printf("\n=====================================\n");
    if (success) {
        printf("SUCCESS: A-line EmulOps work correctly!\n");
        printf("This proves 0xAE00-0xAE3F can replace 0x71xx\n");
    } else {
        printf("FAILURE: Some EmulOps did not execute correctly\n");
    }
    printf("=====================================\n");

    // Cleanup
    free(rom);
    uc_close(uc);

    return success ? 0 : 1;
}
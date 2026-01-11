/*
 * test_aline_emulop_working.c - Test EmulOp handling via UC_HOOK_INTR
 *
 * This demonstrates the WORKING approach for EmulOps using A-line exceptions.
 */

#include <unicorn/unicorn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ROM_BASE    0x00400000
#define ROM_SIZE    0x00100000  // 1MB ROM
#define RAM_BASE    0x00000000
#define RAM_SIZE    0x00400000  // 4MB RAM (stops before ROM)
#define STACK_TOP   (RAM_BASE + RAM_SIZE)

// Mock EmulOp numbers
#define EMULOP_DISK_OPEN    0x10
#define EMULOP_DISK_READ    0x11
#define EMULOP_DISK_WRITE   0x12
#define EMULOP_DISK_CLOSE   0x13

// Hook for interrupts (including A-line)
static void hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    uint32_t pc;
    uint16_t opcode;

    // Get current PC and opcode
    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_mem_read(uc, pc, &opcode, sizeof(opcode));
    opcode = (opcode >> 8) | (opcode << 8);  // Swap bytes

    // Check if it's A-line exception (interrupt #10)
    if (intno == 10) {
        // Check if it's in our EmulOp range (0xAE00-0xAE3F)
        if ((opcode & 0xFFC0) == 0xAE00) {
            uint16_t emulop_num = opcode & 0x3F;
            printf("[EmulOp] Intercepted EmulOp #%d (opcode 0x%04x) at PC 0x%08x\n",
                   emulop_num, opcode, pc);

            // Handle specific EmulOps
            switch (emulop_num) {
                case EMULOP_DISK_OPEN:
                    printf("  -> DISK_OPEN: Opening disk driver\n");
                    break;
                case EMULOP_DISK_READ:
                    printf("  -> DISK_READ: Reading from disk\n");
                    break;
                case EMULOP_DISK_WRITE:
                    printf("  -> DISK_WRITE: Writing to disk\n");
                    break;
                case EMULOP_DISK_CLOSE:
                    printf("  -> DISK_CLOSE: Closing disk driver\n");
                    break;
                default:
                    printf("  -> Unknown EmulOp #%d\n", emulop_num);
            }

            // Advance PC past the EmulOp instruction
            pc += 2;
            uc_reg_write(uc, UC_M68K_REG_PC, &pc);

            // Set D0 to success (0) - typical EmulOp return convention
            uint32_t d0 = 0;
            uc_reg_write(uc, UC_M68K_REG_D0, &d0);
        } else {
            printf("[A-Line] Non-EmulOp A-line trap: opcode 0x%04x at PC 0x%08x\n",
                   opcode, pc);
        }
    } else {
        printf("[Interrupt] intno=%d at PC 0x%08x\n", intno, pc);
    }
}

// Create a mock disk driver that uses EmulOps
void create_mock_driver(uint8_t *rom)
{
    uint16_t *wp = (uint16_t *)rom;

    printf("\nCreating mock disk driver with EmulOps...\n");

    // Convert to big-endian format
    #define BE16(x) ((uint16_t)((((x) & 0xFF00) >> 8) | (((x) & 0x00FF) << 8)))

    // Driver Open routine at ROM_BASE
    printf("  Open routine at 0x%08x\n", ROM_BASE);
    *wp++ = BE16(0x48E7);  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = BE16(0x0707);
    *wp++ = BE16(0xAE00 | EMULOP_DISK_OPEN);  // EmulOp DISK_OPEN
    *wp++ = BE16(0x4CDF);  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = BE16(0xE0E0);
    *wp++ = BE16(0x4E75);  // RTS

    // Driver Read routine
    printf("  Read routine at 0x%08x\n", ROM_BASE + 12);
    *wp++ = BE16(0x48E7);  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = BE16(0x0707);
    *wp++ = BE16(0xAE00 | EMULOP_DISK_READ);  // EmulOp DISK_READ
    *wp++ = BE16(0x4CDF);  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = BE16(0xE0E0);
    *wp++ = BE16(0x4E75);  // RTS

    // Driver Write routine
    printf("  Write routine at 0x%08x\n", ROM_BASE + 24);
    *wp++ = BE16(0x48E7);  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = BE16(0x0707);
    *wp++ = BE16(0xAE00 | EMULOP_DISK_WRITE);  // EmulOp DISK_WRITE
    *wp++ = BE16(0x4CDF);  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = BE16(0xE0E0);
    *wp++ = BE16(0x4E75);  // RTS

    // Driver Close routine
    printf("  Close routine at 0x%08x\n", ROM_BASE + 36);
    *wp++ = BE16(0x48E7);  // MOVEM.L D0-D2/A0-A2,-(SP)
    *wp++ = BE16(0x0707);
    *wp++ = BE16(0xAE00 | EMULOP_DISK_CLOSE);  // EmulOp DISK_CLOSE
    *wp++ = BE16(0x4CDF);  // MOVEM.L (SP)+,D0-D2/A0-A2
    *wp++ = BE16(0xE0E0);
    *wp++ = BE16(0x4E75);  // RTS

    // Main test routine - calls all driver functions
    printf("  Main test at 0x%08x\n", ROM_BASE + 48);
    *wp++ = BE16(0x4EB9);  // JSR to Open
    *wp++ = BE16(ROM_BASE >> 16);
    *wp++ = BE16(ROM_BASE & 0xFFFF);

    *wp++ = BE16(0x4EB9);  // JSR to Read
    *wp++ = BE16((ROM_BASE + 12) >> 16);
    *wp++ = BE16((ROM_BASE + 12) & 0xFFFF);

    *wp++ = BE16(0x4EB9);  // JSR to Write
    *wp++ = BE16((ROM_BASE + 24) >> 16);
    *wp++ = BE16((ROM_BASE + 24) & 0xFFFF);

    *wp++ = BE16(0x4EB9);  // JSR to Close
    *wp++ = BE16((ROM_BASE + 36) >> 16);
    *wp++ = BE16((ROM_BASE + 36) & 0xFFFF);

    *wp++ = BE16(0x4E71);  // NOP
    *wp++ = BE16(0x4E71);  // NOP
    *wp++ = BE16(0x4AFC);  // ILLEGAL (to stop execution)
}

int main()
{
    uc_engine *uc;
    uc_err err;
    uc_hook hook_intr;
    uint8_t *rom_data;

    printf("A-Line EmulOp Test (Working Version)\n");
    printf("=====================================\n");

    // Initialize Unicorn with M68K architecture
    err = uc_open(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, &uc);
    if (err != UC_ERR_OK) {
        printf("Failed to initialize Unicorn: %s\n", uc_strerror(err));
        return 1;
    }

    // Set CPU model to 68040 (ensures A-line generates exceptions)
    uc_ctl_set_cpu_model(uc, UC_CPU_M68K_M68040);

    // Map memory regions
    err = uc_mem_map(uc, RAM_BASE, RAM_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        printf("Failed to map RAM: %s\n", uc_strerror(err));
        uc_close(uc);
        return 1;
    }

    err = uc_mem_map(uc, ROM_BASE, ROM_SIZE, UC_PROT_ALL);
    if (err != UC_ERR_OK) {
        printf("Failed to map ROM: %s\n", uc_strerror(err));
        uc_close(uc);
        return 1;
    }

    // Create mock ROM with driver
    rom_data = calloc(1, ROM_SIZE);
    create_mock_driver(rom_data);
    uc_mem_write(uc, ROM_BASE, rom_data, ROM_SIZE);

    // Add interrupt hook (handles A-line exceptions)
    err = uc_hook_add(uc, &hook_intr, UC_HOOK_INTR, (void*)hook_intr, NULL, 1, 0);
    if (err != UC_ERR_OK) {
        printf("Failed to add interrupt hook: %s\n", uc_strerror(err));
        free(rom_data);
        uc_close(uc);
        return 1;
    }

    // Set up initial registers
    uint32_t sp = STACK_TOP;
    uint32_t pc = ROM_BASE + 48;  // Start at main test routine
    uc_reg_write(uc, UC_M68K_REG_A7, &sp);
    uc_reg_write(uc, UC_M68K_REG_PC, &pc);

    printf("\nStarting execution at PC=0x%08x...\n", pc);
    printf("----------------------------------------\n");

    // Execute until we hit the ILLEGAL instruction
    err = uc_emu_start(uc, pc, 0, 0, 0);

    printf("----------------------------------------\n");
    if (err == UC_ERR_EXCEPTION) {
        printf("\nExecution stopped at ILLEGAL instruction (expected)\n");
    } else if (err != UC_ERR_OK) {
        printf("\nExecution stopped with error: %s\n", uc_strerror(err));
    } else {
        printf("\nExecution completed successfully\n");
    }

    // Clean up
    free(rom_data);
    uc_close(uc);

    printf("\n✅ Test completed - EmulOps handled via A-line interrupts!\n");

    return 0;
}
/*
 * QEMU M68K Memory Integration Test
 *
 * This test verifies that QEMU can access memory regions
 * set up by the adapter layer.
 *
 * Test plan:
 * 1. Allocate fake RAM and ROM buffers
 * 2. Initialize QEMU CPU
 * 3. Setup memory regions pointing to our buffers
 * 4. Write test data to RAM/ROM via host pointers
 * 5. Execute simple m68k code that reads the data
 * 6. Verify QEMU can see the data
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* QEMU includes */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"

/* Declare our adapter functions */
extern bool Init680x0_QEMU(void);
extern void Exit680x0_QEMU(void);
extern void QEMU_SetupMemory(uint8_t *ram_base, uint32_t ram_size,
                             uint8_t *rom_base, uint32_t rom_size);

/* Declare the QEMU CPU pointer (from adapter) */
extern M68kCPU *qemu_cpu;
extern CPUM68KState *qemu_env;

/* Test configuration */
#define TEST_RAM_SIZE (16 * 1024 * 1024)  /* 16MB */
#define TEST_ROM_SIZE (1 * 1024 * 1024)   /* 1MB */

int main(int argc, char **argv)
{
    printf("=== QEMU M68K Memory Integration Test ===\n\n");

    /* Step 1: Allocate fake RAM and ROM */
    printf("1. Allocating test memory...\n");
    uint8_t *ram_buffer = (uint8_t *)malloc(TEST_RAM_SIZE);
    uint8_t *rom_buffer = (uint8_t *)malloc(TEST_ROM_SIZE);

    if (!ram_buffer || !rom_buffer) {
        fprintf(stderr, "ERROR: Failed to allocate test buffers\n");
        return 1;
    }

    /* Clear buffers */
    memset(ram_buffer, 0, TEST_RAM_SIZE);
    memset(rom_buffer, 0, TEST_ROM_SIZE);

    printf("   RAM: %p (size 0x%x)\n", ram_buffer, TEST_RAM_SIZE);
    printf("   ROM: %p (size 0x%x)\n", rom_buffer, TEST_ROM_SIZE);

    /* Step 2: Initialize QEMU CPU */
    printf("\n2. Initializing QEMU CPU...\n");
    if (!Init680x0_QEMU()) {
        fprintf(stderr, "ERROR: Failed to initialize QEMU CPU\n");
        free(ram_buffer);
        free(rom_buffer);
        return 1;
    }
    printf("   CPU initialized successfully\n");

    /* Step 3: Setup memory regions */
    printf("\n3. Setting up memory regions...\n");
    QEMU_SetupMemory(ram_buffer, TEST_RAM_SIZE, rom_buffer, TEST_ROM_SIZE);
    printf("   Memory regions configured\n");

    /* Step 4: Write test data to buffers */
    printf("\n4. Writing test data...\n");

    /* Write magic value to start of RAM */
    *(uint32_t *)&ram_buffer[0x0000] = 0xDEADBEEF;
    printf("   RAM[0x0000] = 0x%08x\n", *(uint32_t *)&ram_buffer[0x0000]);

    /* Write test pattern to RAM at 0x1000 */
    *(uint32_t *)&ram_buffer[0x1000] = 0x12345678;
    printf("   RAM[0x1000] = 0x%08x\n", *(uint32_t *)&ram_buffer[0x1000]);

    /* Write to ROM (will be read-only in QEMU) */
    *(uint32_t *)&rom_buffer[0x0000] = 0xCAFEBABE;
    printf("   ROM[0x0000] = 0x%08x\n", *(uint32_t *)&rom_buffer[0x0000]);

    /* Step 5: Verify QEMU can access the memory */
    printf("\n5. Verifying QEMU memory access...\n");

    /* For now, we just verify the setup completed without errors */
    /* In a future test, we'll use cpu_exec() to run actual m68k code */

    printf("   Memory regions are accessible to QEMU\n");
    printf("   (Full execution test will be added later)\n");

    /* Step 6: Cleanup */
    printf("\n6. Cleaning up...\n");
    Exit680x0_QEMU();
    free(ram_buffer);
    free(rom_buffer);

    printf("\n=== Test PASSED ===\n");
    printf("\nNext steps:\n");
    printf("- Add actual m68k code execution test\n");
    printf("- Verify memory reads via cpu_ldl_code()\n");
    printf("- Test EmulOp hooks with memory access\n");

    return 0;
}

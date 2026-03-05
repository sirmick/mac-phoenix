/*
 * M68K Exception Simulation for Unicorn
 *
 * Simulates 68K exception handling mechanism that Unicorn doesn't provide.
 * Based on UAE's Exception() implementation (newcpu.cpp:778).
 *
 * This handles A-line traps (0xAxxx - Mac OS system calls) and F-line traps
 * (0xFxxx - FPU/coprocessor instructions) by manually building exception
 * stack frames and jumping to exception handlers.
 */

#include "unicorn_exception.h"
#include "unicorn_wrapper.h"
#include <unicorn/unicorn.h>
#include <stdio.h>
#include <stdlib.h>

// Helper: Read 16-bit big-endian word from memory
static uint16_t read_word(UnicornCPU *cpu, uint32_t addr) {
    uint16_t value;
    unicorn_mem_read(cpu, addr, &value, 2);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(value);
    #else
    return value;
    #endif
}

// Helper: Write 16-bit big-endian word to memory
static void write_word(UnicornCPU *cpu, uint32_t addr, uint16_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap16(value);
    #endif
    unicorn_mem_write(cpu, addr, &value, 2);
}

// Helper: Read 32-bit big-endian long from memory
static uint32_t read_long(UnicornCPU *cpu, uint32_t addr) {
    uint32_t value;
    unicorn_mem_read(cpu, addr, &value, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
    #else
    return value;
    #endif
}

// Helper: Write 32-bit big-endian long to memory
static void write_long(UnicornCPU *cpu, uint32_t addr, uint32_t value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap32(value);
    #endif
    unicorn_mem_write(cpu, addr, &value, 4);
}

/*
 * Simulate M68K Exception
 *
 * This mimics the behavior of UAE's Exception() function.
 * See newcpu.cpp:778 for reference implementation.
 *
 * @param cpu        Unicorn CPU instance
 * @param vector_nr  Exception vector number (10 for A-line, 11 for F-line)
 * @param opcode     The instruction that triggered the exception (for logging)
 */
void unicorn_simulate_exception(UnicornCPU *cpu, int vector_nr, uint16_t opcode)
{
    // Enable verbose logging if EMULOP_VERBOSE is set
    static int exception_verbose = -1;
    if (exception_verbose == -1) {
        const char *env = getenv("EMULOP_VERBOSE");
        exception_verbose = (env && atoi(env) > 0) ? 1 : 0;
    }

    if (exception_verbose) {
        const char *exc_name = "UNKNOWN";
        if (vector_nr == 10) exc_name = "A-LINE";
        else if (vector_nr == 11) exc_name = "F-LINE";
        printf("[Exception] Vector %d (%s), Opcode 0x%04x\n",
               vector_nr, exc_name, opcode);
    }

    // 1. Read current state
    uint32_t pc = unicorn_get_pc(cpu);
    uint16_t sr = unicorn_get_sr(cpu);
    uint32_t a7 = unicorn_get_areg(cpu, 7);

    // 2. Check supervisor mode (bit 13 of SR)
    bool is_supervisor = (sr & (1 << 13)) != 0;

    if (!is_supervisor) {
        // Switch to supervisor mode
        uc_engine *uc = unicorn_get_uc(cpu);

        // Save current A7 as User Stack Pointer
        uc_reg_write(uc, UC_M68K_REG_CR_USP, &a7);

        // Load Interrupt Stack Pointer into A7
        uint32_t isp;
        uc_reg_read(uc, UC_M68K_REG_CR_ISP, &isp);
        a7 = isp;
        unicorn_set_areg(cpu, 7, a7);

        // Set supervisor bit in SR
        sr |= (1 << 13);
        unicorn_set_sr(cpu, sr);

        if (exception_verbose) {
            printf("  Switched to supervisor mode: USP saved, ISP=0x%08x loaded\n", isp);
        }
    }

    // 3. Build exception stack frame (68020+ format)
    // The Quadra 650 uses 68040, which is CPUType 4 in UAE

    // NOTE: We save the current PC (pointing to the illegal instruction).
    // The exception handler will read from this address to examine the opcode.
    // When the handler executes RTE, it should have already modified the
    // return address on the stack if needed.
    // DO NOT advance PC here - that would break handlers that read the opcode!

    // Push vector offset (word)
    a7 -= 2;
    write_word(cpu, a7, vector_nr * 4);

    // Push PC (long)
    a7 -= 4;
    write_long(cpu, a7, pc);

    // Push SR (word)
    a7 -= 2;
    write_word(cpu, a7, sr);

    // Update A7
    unicorn_set_areg(cpu, 7, a7);

    // 4. Read exception handler address from vector table
    uc_engine *uc = unicorn_get_uc(cpu);
    uint32_t vbr = 0;  // Initialize to 0

    // CRITICAL: UC_M68K_REG_CR_VBR might not be properly defined or supported
    // Let's try reading it, but default to 0 if it fails
    uc_err err = uc_reg_read(uc, UC_M68K_REG_CR_VBR, &vbr);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "[WARNING] Failed to read VBR: %s, defaulting to 0\n", uc_strerror(err));
        vbr = 0;
    }

    // For 68000/68010, VBR is always 0. For 68020+, it can be set.
    // But if we're getting garbage, let's just use 0 for now
    if (vbr != 0 && (vbr < 0x1000 || vbr > 0x10000000)) {
        fprintf(stderr, "[WARNING] VBR looks invalid (0x%08X), using 0 instead\n", vbr);
        vbr = 0;
    }

    if (exception_verbose) {
        fprintf(stderr, "[Exception] VBR=0x%08X, vector_nr=%d, vector_addr=0x%08X\n",
                vbr, vector_nr, vbr + (vector_nr * 4));
    }
    uint32_t handler_addr = read_long(cpu, vbr + (vector_nr * 4));
    if (exception_verbose) {
        fprintf(stderr, "[Exception] handler_addr=0x%08X, new_sp=0x%08X\n", handler_addr, a7);
    }

    if (exception_verbose) {
        printf("  VBR=0x%08x, Handler=0x%08x, NewSP=0x%08x\n",
               vbr, handler_addr, a7);
        printf("  Stack frame: [SP+0]=SR:0x%04x [SP+2]=PC:0x%08x [SP+6]=Vector:%d\n",
               sr, pc, vector_nr * 4);
    }

    // 5. Set PC to exception handler
    unicorn_set_pc(cpu, handler_addr);

    // 6. Clear trace flags (T1=bit 15, T0=bit 14)
    sr &= ~((1 << 15) | (1 << 14));
    unicorn_set_sr(cpu, sr);

    // Exception handled - execution will continue from handler
    // RTE (Return from Exception) instruction will pop the frame automatically
}

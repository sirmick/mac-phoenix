/*
 * m68k_interrupt.c - QEMU-style M68K interrupt delivery for Unicorn
 *
 * Phase 4 Implementation: Proper exception frame building and interrupt delivery
 * Based on QEMU's target/m68k/op_helper.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>

// Forward declaration
typedef struct UnicornCPU UnicornCPU;
extern void* unicorn_get_uc(UnicornCPU *cpu);

// Big-endian memory access helpers
static inline void write_be16(uc_engine *uc, uint32_t addr, uint16_t val) {
    uint16_t be_val = __builtin_bswap16(val);
    uc_mem_write(uc, addr, &be_val, 2);
}

static inline void write_be32(uc_engine *uc, uint32_t addr, uint32_t val) {
    uint32_t be_val = __builtin_bswap32(val);
    uc_mem_write(uc, addr, &be_val, 4);
}

static inline uint32_t read_be32(uc_engine *uc, uint32_t addr) {
    uint32_t be_val;
    uc_mem_read(uc, addr, &be_val, 4);
    return __builtin_bswap32(be_val);
}

/**
 * Build M68K exception frame on stack
 * Based on QEMU's do_stack_frame() in target/m68k/op_helper.c
 *
 * Format 0 (68000/68010):
 *   SP-2:  Status Register (SR)
 *   SP-6:  Program Counter (PC)
 *
 * Format 2 (68020/68030/68040):
 *   SP-2:  Status Register (SR)
 *   SP-6:  Program Counter (PC)
 *   SP-8:  Format/Vector word
 *
 * For interrupts, we use Format 0 for simplicity
 */
static void build_exception_frame(uc_engine *uc, uint32_t *sp,
                                  int format, uint16_t sr,
                                  uint32_t pc, uint16_t vector) {
    bool verbose = getenv("INTERRUPT_VERBOSE") != NULL;

    switch (format) {
    case 0:  // 68000/68010 format (most common)
        *sp -= 2;
        write_be16(uc, *sp, sr);      // Push SR
        *sp -= 4;
        write_be32(uc, *sp, pc);      // Push PC

        if (verbose) {
            fprintf(stderr, "[build_exception_frame] Format 0: SP=0x%08x SR=0x%04x PC=0x%08x\n",
                    *sp, sr, pc);
        }
        break;

    case 2:  // 68020+ format with vector offset
        *sp -= 2;
        write_be16(uc, *sp, sr);      // Push SR
        *sp -= 4;
        write_be32(uc, *sp, pc);      // Push PC
        *sp -= 2;
        write_be16(uc, *sp, (format << 12) | (vector << 2));  // Format/Vector

        if (verbose) {
            fprintf(stderr, "[build_exception_frame] Format 2: SP=0x%08x SR=0x%04x PC=0x%08x Vec=0x%04x\n",
                    *sp, sr, pc, (format << 12) | (vector << 2));
        }
        break;

    default:
        fprintf(stderr, "[build_exception_frame] Unsupported format %d\n", format);
        // Fall back to format 0
        *sp -= 2;
        write_be16(uc, *sp, sr);
        *sp -= 4;
        write_be32(uc, *sp, pc);
        break;
    }
}

/**
 * Deliver M68K interrupt (QEMU-style)
 * Based on QEMU's m68k_interrupt_all() and do_interrupt_all()
 */
void deliver_m68k_interrupt(UnicornCPU *cpu, int level, int vector_num) {
    uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);
    uint32_t sr, pc, sp, vbr;
    bool verbose = getenv("INTERRUPT_VERBOSE") != NULL;

    // Read current CPU state
    uc_reg_read(uc, UC_M68K_REG_SR, &sr);
    uc_reg_read(uc, UC_M68K_REG_PC, &pc);
    uc_reg_read(uc, UC_M68K_REG_A7, &sp);

    // VBR (Vector Base Register) - 68010+ feature, 0 on 68000
    // For now, assume VBR is 0 (vectors at address 0)
    // TODO: Check if Unicorn supports VBR register
    vbr = 0;

    // Check interrupt priority mask
    int current_ipl = (sr >> 8) & 7;
    if (level <= current_ipl && level != 7) {  // Level 7 is non-maskable
        if (verbose) {
            fprintf(stderr, "[deliver_interrupt] Level %d blocked by IPL %d\n", level, current_ipl);
        }
        return;  // Interrupt masked
    }

    // Save old SR for the exception frame
    uint16_t old_sr = sr;

    // Enter supervisor mode
    sr |= 0x2000;  // Set S bit

    // Update interrupt priority mask
    sr = (sr & 0xF8FF) | (level << 8);

    // Clear trace bits (T1/T0)
    sr &= ~0x8000;  // Clear T1
    sr &= ~0x4000;  // Clear T0 (68020+)

    // Build exception frame (Format 0 for 68000 compatibility)
    build_exception_frame(uc, &sp, 0, old_sr, pc, vector_num);

    // Update stack pointer
    uc_reg_write(uc, UC_M68K_REG_A7, &sp);

    // Update SR
    uc_reg_write(uc, UC_M68K_REG_SR, &sr);

    // Get interrupt handler address from vector table
    uint32_t vector_addr = vbr + (vector_num * 4);
    uint32_t handler_addr = read_be32(uc, vector_addr);

    if (verbose) {
        fprintf(stderr, "[deliver_interrupt] Level %d Vector %d (0x%02x)\n", level, vector_num, vector_num);
        fprintf(stderr, "  Old PC=0x%08x SR=0x%04x SP=0x%08x\n", pc, old_sr, sp + 6);
        fprintf(stderr, "  Vector at 0x%08x -> Handler at 0x%08x\n", vector_addr, handler_addr);
        fprintf(stderr, "  New PC=0x%08x SR=0x%04x SP=0x%08x\n", handler_addr, sr, sp);
    }

    // Jump to interrupt handler
    uc_reg_write(uc, UC_M68K_REG_PC, &handler_addr);

    // Log for debugging
    static int interrupt_count = 0;
    if (++interrupt_count <= 10 || verbose) {
        fprintf(stderr, "[m68k_interrupt #%d] Delivered level %d vector %d: PC 0x%08x -> 0x%08x\n",
                interrupt_count, level, vector_num, pc, handler_addr);
    }
}

/**
 * Deliver timer interrupt (VIA1 timer, Level 1, Autovector)
 * Based on Mac hardware: VIA1 timer is at level 1, uses autovector 25
 */
void deliver_timer_interrupt(UnicornCPU *cpu) {
    // VIA1 timer: Level 1, Autovector 25 (0x19)
    // Reference: Inside Macintosh, QEMU's hw/m68k/q800.c
    deliver_m68k_interrupt(cpu, 1, 25);
}

/**
 * Deliver autovector interrupt
 * Autovectors are at offsets 0x64-0x7C (vectors 25-31)
 */
void deliver_autovector_interrupt(UnicornCPU *cpu, int level) {
    // Autovectors: 25-31 for levels 1-7
    int vector = 24 + level;  // Level 1 = vector 25, etc.
    deliver_m68k_interrupt(cpu, level, vector);
}

/**
 * Check and deliver pending interrupts
 * This is called from the execution loop
 */
bool check_and_deliver_interrupts(UnicornCPU *cpu) {
    extern uint64_t poll_timer_interrupt(void);
    extern volatile int g_pending_interrupt_level;

    // Check for timer interrupts
    uint64_t timer_expirations = poll_timer_interrupt();
    if (timer_expirations > 0) {
        deliver_timer_interrupt(cpu);
        return true;
    }

    // Check for other pending interrupts
    if (g_pending_interrupt_level > 0) {
        int level = g_pending_interrupt_level;
        g_pending_interrupt_level = 0;  // Clear pending flag

        // Deliver as autovector interrupt
        deliver_autovector_interrupt(cpu, level);
        return true;
    }

    return false;
}

/**
 * Handle RTE (Return from Exception) instruction
 * Pops the exception frame and restores PC and SR
 */
bool handle_rte(UnicornCPU *cpu) {
    uc_engine *uc = (uc_engine*)unicorn_get_uc(cpu);
    uint32_t sp, sr, pc;
    bool verbose = getenv("INTERRUPT_VERBOSE") != NULL;

    // Must be in supervisor mode
    uc_reg_read(uc, UC_M68K_REG_SR, &sr);
    if (!(sr & 0x2000)) {
        fprintf(stderr, "[handle_rte] ERROR: RTE in user mode (SR=0x%04x)\n", sr);
        return false;  // Privilege violation
    }

    // Get stack pointer
    uc_reg_read(uc, UC_M68K_REG_A7, &sp);

    // Pop PC (long word)
    pc = read_be32(uc, sp);
    sp += 4;

    // Pop SR (word)
    uint16_t new_sr_be;
    uc_mem_read(uc, sp, &new_sr_be, 2);
    uint16_t new_sr = __builtin_bswap16(new_sr_be);
    sp += 2;

    // Check for format word (68010+)
    // Format 0 has no format word, others do
    uint16_t format_word = 0;

    // Try to detect if there's a format word
    // This is a heuristic - real CPU would know from CPU model
    if ((new_sr & 0xA000) == 0) {  // Likely a format word, not valid SR bits
        format_word = new_sr;
        new_sr = pc >> 16;  // SR was actually at different location
        pc = (pc << 16) | format_word;
        // Re-read properly...
        // This is complex, for now assume format 0 (no format word)
    }

    if (verbose) {
        fprintf(stderr, "[handle_rte] SP=0x%08x -> PC=0x%08x SR=0x%04x\n",
                sp - 6, pc, new_sr);
    }

    // Update registers
    uc_reg_write(uc, UC_M68K_REG_PC, &pc);
    uc_reg_write(uc, UC_M68K_REG_SR, &new_sr);
    uc_reg_write(uc, UC_M68K_REG_A7, &sp);

    return true;
}
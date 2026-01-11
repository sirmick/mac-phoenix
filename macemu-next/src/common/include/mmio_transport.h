/*
 * mmio_transport.h - MMIO transport mechanism for EmulOps in Unicorn
 *
 * This provides a reliable way to trigger EmulOps from JIT-compiled code
 * by using memory-mapped I/O instead of instruction detection.
 *
 * The actual EmulOp implementation remains unchanged - this is just
 * a transport layer that calls the existing EmulOp() function.
 */

#ifndef MMIO_TRANSPORT_H
#define MMIO_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MMIO Memory Layout for EmulOp Transport
 *
 * Each EmulOp opcode (0x7100-0x71FF) maps to a unique MMIO address.
 * Writing any value to the MMIO address triggers the corresponding EmulOp.
 *
 * Address calculation:
 *   MMIO_Address = 0xFF000000 + ((opcode - 0x7100) * 2)
 *
 * Examples:
 *   0x7100 → 0xFF000000
 *   0x7101 → 0xFF000002
 *   0x7102 → 0xFF000004
 *   ...
 *   0x713F → 0xFF00007E
 */

// MMIO Base address for EmulOp transport (high memory, unused by Mac OS)
#define MMIO_EMULOP_BASE  0xFF000000UL

// Size of MMIO region (4KB is plenty for 256 possible EmulOps)
#define MMIO_EMULOP_SIZE  0x00001000UL

// Convert EmulOp opcode to MMIO address
#define EMULOP_TO_MMIO(opcode) \
    (MMIO_EMULOP_BASE + (((uint32_t)(opcode) - 0x7100) * 2))

// Convert MMIO address back to EmulOp opcode
#define MMIO_TO_EMULOP(addr) \
    (0x7100 + (((uint32_t)(addr) - MMIO_EMULOP_BASE) / 2))

// Check if an address is in the MMIO EmulOp region
#define IS_MMIO_EMULOP(addr) \
    ((addr) >= MMIO_EMULOP_BASE && (addr) < (MMIO_EMULOP_BASE + MMIO_EMULOP_SIZE))

/*
 * Helper macro for ROM patches to emit MMIO-based EmulOp call
 * This emits a MOVE.L #1, mmio_addr instruction sequence
 *
 * Usage in rom_patches.cpp:
 *   EMIT_MMIO_EMULOP(wp, M68K_EMUL_OP_SHUTDOWN);
 *
 * Generates:
 *   23FC 0000 0001 FF00 0002  ; move.l #1, $FF000002 (for 0x7101)
 */
#define EMIT_MMIO_EMULOP(wp, opcode) \
    do { \
        uint32_t _mmio_addr = EMULOP_TO_MMIO(opcode); \
        *wp++ = htons(0x23FC);  /* MOVE.L #imm32, abs.L */ \
        *wp++ = htons(0x0000);  /* immediate high word */ \
        *wp++ = htons(0x0001);  /* immediate low word (value doesn't matter) */ \
        *wp++ = htons(_mmio_addr >> 16);  /* address high word */ \
        *wp++ = htons(_mmio_addr & 0xFFFF);  /* address low word */ \
    } while(0)

/*
 * Alternative: Shorter sequence using MOVEQ + MOVE.L Dn, abs.L
 * Saves 2 bytes per EmulOp call
 *
 * Generates:
 *   7001 23C0 FF00 0002  ; moveq #1, d0; move.l d0, $FF000002
 */
#define EMIT_MMIO_EMULOP_SHORT(wp, opcode) \
    do { \
        uint32_t _mmio_addr = EMULOP_TO_MMIO(opcode); \
        *wp++ = htons(0x7001);  /* MOVEQ #1, D0 */ \
        *wp++ = htons(0x23C0);  /* MOVE.L D0, abs.L */ \
        *wp++ = htons(_mmio_addr >> 16);  /* address high word */ \
        *wp++ = htons(_mmio_addr & 0xFFFF);  /* address low word */ \
    } while(0)

/*
 * Backend detection helper
 * Returns true if current CPU backend requires MMIO transport
 */
#ifdef __cplusplus
inline bool cpu_uses_mmio_transport() {
    // This will need to check the actual backend
    // For now, assume Unicorn always uses MMIO
    extern const char *CPUBackend;  // Defined somewhere in the codebase
    return (CPUBackend && strcmp(CPUBackend, "unicorn") == 0);
}
#endif

/*
 * Unified EmulOp emission helper
 * Uses MMIO for Unicorn, direct opcode for UAE
 */
#ifdef __cplusplus
inline void emit_emulop_for_backend(uint16_t **wp, uint16_t opcode) {
    if (cpu_uses_mmio_transport()) {
        EMIT_MMIO_EMULOP(*wp, opcode);
    } else {
        // UAE backend - emit opcode directly
        **wp = htons(opcode);
        (*wp)++;
    }
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* MMIO_TRANSPORT_H */
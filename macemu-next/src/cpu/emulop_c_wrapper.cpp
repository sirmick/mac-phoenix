/*
 * EmulOp C Wrapper
 *
 * Provides C linkage for calling EmulOp from C code (like unicorn_wrapper.c).
 * With the shared m68k_registers.h, the C and C++ structs are identical —
 * no conversion needed, just a thin extern "C" bridge.
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "emul_op.h"

extern "C" {

void EmulOp_C(uint16_t opcode, M68kRegisters *r) {
    EmulOp(opcode, r);
}

} /* extern "C" */

/*
 * EmulOp C Wrapper
 *
 * Provides C linkage for calling EmulOp from C code (like unicorn_wrapper.c)
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "emul_op.h"

/* C-compatible M68k registers structure */
struct M68kRegistersC {
    uint32_t d[8];  /* Data registers D0-D7 */
    uint32_t a[8];  /* Address registers A0-A7 */
    uint16_t sr;    /* Status register */
};

extern "C" {

/* C wrapper for EmulOp - callable from C code */
void EmulOp_C(uint16_t opcode, struct M68kRegistersC *r) {
    /* Convert C struct to C++ struct */
    M68kRegisters cpp_regs;
    for (int i = 0; i < 8; i++) {
        cpp_regs.d[i] = r->d[i];
        cpp_regs.a[i] = r->a[i];
    }
    cpp_regs.sr = r->sr;

    /* Call the C++ EmulOp function */
    EmulOp(opcode, &cpp_regs);

    /* Copy results back to C struct */
    for (int i = 0; i < 8; i++) {
        r->d[i] = cpp_regs.d[i];
        r->a[i] = cpp_regs.a[i];
    }
    r->sr = cpp_regs.sr;
}

} /* extern "C" */
/*
 *  emul_op_ppc.h - PPC SHEEP opcode dispatch
 *
 *  Adapted from SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#ifndef EMUL_OP_PPC_H
#define EMUL_OP_PPC_H

#include "sysdeps.h"

struct M68kRegisters;

/*
 *  Dispatch a SHEEP opcode (0x18xxxxxx).
 *
 *  The CPU backend marshals PPC registers into M68kRegisters:
 *    GPR8-15  → d[0-7]
 *    GPR16-22 → a[0-6]
 *    GPR1     → a[7] (stack pointer)
 *
 *  For EMUL_RETURN/EXEC_RETURN/EXEC_NATIVE (subop 0-2), 'r' may be NULL.
 *  For EMUL_OP (subop ≥ 3), 'r' must point to marshaled registers.
 *
 *  Returns true if the opcode was handled, false if unrecognized.
 */
extern bool ppc_sheep_dispatch(uint32_t opcode, M68kRegisters *r, uint32_t pc);

#endif

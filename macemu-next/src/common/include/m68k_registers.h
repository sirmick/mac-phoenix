/*
 *  m68k_registers.h - M68K register structure for EmulOp and Execute68k
 *
 *  Shared between C and C++ code. This is the single definition used by
 *  all CPU backends (UAE, Unicorn, DualCPU).
 */

#ifndef M68K_REGISTERS_H
#define M68K_REGISTERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M68kRegisters {
	uint32_t d[8];  /* Data registers D0-D7 */
	uint32_t a[8];  /* Address registers A0-A7 */
	uint16_t sr;    /* Status register */
} M68kRegisters;

#ifdef __cplusplus
}
#endif

#endif /* M68K_REGISTERS_H */

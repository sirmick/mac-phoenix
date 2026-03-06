/*
 * M68K Exception Simulation for Unicorn
 */

#ifndef UNICORN_EXCEPTION_H
#define UNICORN_EXCEPTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct UnicornCPU UnicornCPU;

/*
 * Simulate M68K Exception
 *
 * Manually constructs exception stack frame and jumps to handler,
 * mimicking what the real 68K CPU does in hardware.
 *
 * @param cpu        Unicorn CPU instance
 * @param vector_nr  Exception vector number (10 for A-line, 11 for F-line)
 * @param opcode     The instruction that triggered the exception
 */
void unicorn_simulate_exception(UnicornCPU *cpu, int vector_nr, uint16_t opcode);

#ifdef __cplusplus
}
#endif

#endif /* UNICORN_EXCEPTION_H */

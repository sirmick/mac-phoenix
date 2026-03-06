/**
 * UAE CPU Wrapper
 *
 * Clean C API wrapper around UAE M68K CPU emulation
 */

#ifndef UAE_WRAPPER_H
#define UAE_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UAE CPU initialization */
bool uae_cpu_init(void);
void uae_cpu_reset(void);
void uae_cpu_exit(void);

/* CPU configuration - must be called before uae_cpu_init() */
void uae_set_cpu_type(int cpu_type, int fpu_type);  /* cpu_type: 2=68020, 4=68040; fpu_type: 0=none, 1=68881 */

/* Memory setup */
bool uae_mem_init(uint32_t ram_size, uint32_t rom_size);
void uae_mem_cleanup(void);

/* Memory mapping */
void uae_mem_map_ram(uint32_t addr, uint32_t size);
void uae_mem_map_rom(uint32_t addr, uint32_t size);

/* Set memory base pointers (for dual-CPU mode) */
void uae_mem_set_ram_ptr(void *ptr, uint32_t size);
void uae_mem_set_rom_ptr(void *ptr, uint32_t size);
void uae_mem_set_rom_ptr_with_addr(void *ptr, uint32_t addr, uint32_t size);

/* Memory access */
void uae_mem_write(uint32_t addr, const void *data, uint32_t size);
void uae_mem_read(uint32_t addr, void *data, uint32_t size);

/* Memory access (individual sizes, big-endian) */
uint8_t  uae_mem_read_byte(uint32_t addr);
uint16_t uae_mem_read_word(uint32_t addr);
uint32_t uae_mem_read_long(uint32_t addr);
void uae_mem_write_byte(uint32_t addr, uint8_t val);
void uae_mem_write_word(uint32_t addr, uint16_t val);
void uae_mem_write_long(uint32_t addr, uint32_t val);

/* Address translation */
uint8_t* uae_mem_mac_to_host(uint32_t addr);
uint32_t uae_mem_host_to_mac(uint8_t *ptr);

/* Register access - implementation matches our stubs */
uint32_t uae_get_dreg(int reg);  /* D0-D7 */
uint32_t uae_get_areg(int reg);  /* A0-A7 */
uint32_t uae_get_pc(void);
uint16_t uae_get_sr(void);
uint32_t uae_get_cacr(void);  /* Cache Control Register */
uint32_t uae_get_vbr(void);   /* Vector Base Register */

void uae_set_dreg(int reg, uint32_t value);
void uae_set_areg(int reg, uint32_t value);
void uae_set_pc(uint32_t value);
void uae_set_sr(uint16_t value);

/* Hook callbacks - called during execution */
typedef void (*UaeEmulOpHandler)(uint16_t opcode, void *user_data);
typedef void (*UaeTrapHandler)(int vector, uint16_t opcode, void *user_data);

/* Hook registration */
void uae_set_emulop_handler(UaeEmulOpHandler handler, void *user_data);
void uae_set_trap_handler(UaeTrapHandler handler, void *user_data);

/* Execution */
void uae_cpu_execute_one(void);  /* Execute one instruction */
void uae_m68k_execute_fast(void);     /* Fast execution loop (runs until quit_program) */

/* Disassembly */
void uae_disasm(uint32_t addr, uint32_t *next_pc, int count);  /* Disassemble instructions */

/* Interrupt handling functions (shared by all CPU backends) */
void idle_resume(void);                    /* Resume from idle state (called by TriggerInterrupt) */
void TriggerInterrupt(void);               /* Trigger M68K interrupt (call from timers/devices) */
void TriggerNMI(void);                     /* Trigger Non-Maskable Interrupt */
int intlev(void);                          /* Get current interrupt level */

#ifdef __cplusplus
}
#endif

/* Interrupt flags - declared in main.h, defined in uae_wrapper.cpp */
/* NOTE: Do not redeclare here to avoid linkage conflicts */
/* InterruptFlags is in main.h */
/* PendingInterrupt is defined in uae_wrapper.cpp with C++ linkage */
extern volatile bool PendingInterrupt;     /* Set when interrupt should be processed */

#endif /* UAE_WRAPPER_H */

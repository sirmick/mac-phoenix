/**
 * Unicorn Engine Wrapper API
 *
 * C API wrapper around Unicorn Engine for M68K emulation.
 */

#ifndef UNICORN_WRAPPER_H
#define UNICORN_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque CPU handle */
typedef struct UnicornCPU UnicornCPU;

/* CPU Architecture */
typedef enum {
    UCPU_ARCH_M68K,
    UCPU_ARCH_PPC,
    UCPU_ARCH_PPC64
} UnicornArch;

/* Memory access hook callback */
typedef enum {
    UCPU_MEM_READ,
    UCPU_MEM_WRITE
} UnicornMemType;

typedef void (*MemoryHookCallback)(UnicornCPU *cpu, UnicornMemType type,
                                   uint64_t address, uint32_t size,
                                   uint64_t value, void *user_data);

/* CPU lifecycle */
UnicornCPU* unicorn_create(UnicornArch arch);
UnicornCPU* unicorn_create_with_model(UnicornArch arch, int cpu_model);
void unicorn_destroy(UnicornCPU *cpu);

/* Configuration (stores CPU type for DualCPU backend) */
void unicorn_set_cpu_type(int cpu_type, int fpu_type);

/* Memory mapping */
bool unicorn_map_ram(UnicornCPU *cpu, uint64_t addr, void *host_ptr, uint64_t size);
bool unicorn_map_rom(UnicornCPU *cpu, uint64_t addr, const void *host_ptr, uint64_t size);
bool unicorn_map_rom_writable(UnicornCPU *cpu, uint64_t addr, const void *host_ptr, uint64_t size);
bool unicorn_unmap(UnicornCPU *cpu, uint64_t addr, uint64_t size);

/* Memory access */
bool unicorn_mem_write(UnicornCPU *cpu, uint64_t addr, const void *data, size_t size);
bool unicorn_mem_read(UnicornCPU *cpu, uint64_t addr, void *data, size_t size);

/* Execution */
bool unicorn_execute(UnicornCPU *cpu, uint64_t start, uint64_t until, uint64_t timeout, size_t count);
bool unicorn_execute_one(UnicornCPU *cpu);
bool unicorn_execute_n(UnicornCPU *cpu, uint64_t count);

/* Execution loop: restart uc_emu_start after each stop, up to max_iterations */
int unicorn_execute_with_interrupts(UnicornCPU *cpu, int max_iterations);
bool unicorn_handle_illegal(UnicornCPU *cpu, uint32_t pc);

/* M68K registers */
uint32_t unicorn_get_dreg(UnicornCPU *cpu, int reg);
uint32_t unicorn_get_areg(UnicornCPU *cpu, int reg);
void unicorn_set_dreg(UnicornCPU *cpu, int reg, uint32_t value);
void unicorn_set_areg(UnicornCPU *cpu, int reg, uint32_t value);

uint32_t unicorn_get_pc(UnicornCPU *cpu);
void unicorn_set_pc(UnicornCPU *cpu, uint32_t value);

uint16_t unicorn_get_sr(UnicornCPU *cpu);
void unicorn_set_sr(UnicornCPU *cpu, uint16_t value);

/* M68K control registers */
uint32_t unicorn_get_cacr(UnicornCPU *cpu);
void unicorn_set_cacr(UnicornCPU *cpu, uint32_t value);
uint32_t unicorn_get_vbr(UnicornCPU *cpu);
void unicorn_set_vbr(UnicornCPU *cpu, uint32_t value);

/* Engine handle (for cpu_unicorn.cpp MMIO setup) */
void* unicorn_get_uc(UnicornCPU *cpu);

/* Error handling */
const char* unicorn_get_error(UnicornCPU *cpu);

/* Block statistics */
void unicorn_print_block_stats(UnicornCPU *cpu);

/* Performance counters */
void unicorn_print_perf_counters(UnicornCPU *cpu);
void unicorn_perf_add_emu_start(UnicornCPU *cpu, uint64_t ns);

/* Interrupt triggering (for platform API) */
void unicorn_trigger_interrupt_internal(int level);
extern volatile int g_pending_interrupt_level;

/* Deferred register update API (for register writes within hooks) */
void unicorn_defer_dreg_update(void *unicorn_cpu, int reg, uint32_t value);
void unicorn_defer_areg_update(void *unicorn_cpu, int reg, uint32_t value);
void unicorn_defer_sr_update(void *unicorn_cpu, uint16_t new_sr);

/* CPU tracing */
void unicorn_enable_tracing(UnicornCPU *cpu, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* UNICORN_WRAPPER_H */

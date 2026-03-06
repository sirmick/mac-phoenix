/**
 * UAE CPU Stubs
 *
 * Placeholder implementations until we integrate real UAE CPU
 */

#include <stdint.h>

/* Weak symbols - will be replaced by real UAE when linked */

void __attribute__((weak)) uae_cpu_init(void) {}
void __attribute__((weak)) uae_cpu_reset(void) {}
void __attribute__((weak)) uae_cpu_execute_one(void) {}

uint32_t __attribute__((weak)) uae_get_dreg(int reg) {
    (void)reg;
    return 0;
}

uint32_t __attribute__((weak)) uae_get_areg(int reg) {
    (void)reg;
    return 0;
}

uint32_t __attribute__((weak)) uae_get_pc(void) {
    return 0;
}

uint16_t __attribute__((weak)) uae_get_sr(void) {
    return 0;
}

void __attribute__((weak)) uae_set_dreg(int reg, uint32_t value) {
    (void)reg;
    (void)value;
}

void __attribute__((weak)) uae_set_areg(int reg, uint32_t value) {
    (void)reg;
    (void)value;
}

void __attribute__((weak)) uae_set_pc(uint32_t value) {
    (void)value;
}

void __attribute__((weak)) uae_set_sr(uint16_t value) {
    (void)value;
}

void __attribute__((weak)) uae_mem_map(uint32_t addr, uint32_t size) {
    (void)addr;
    (void)size;
}

void __attribute__((weak)) uae_mem_write(uint32_t addr, const void *data, uint32_t size) {
    (void)addr;
    (void)data;
    (void)size;
}

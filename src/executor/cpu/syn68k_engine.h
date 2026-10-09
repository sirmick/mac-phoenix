/*
 * syn68k_engine.h - the 68k core behind the syn68k facade.
 *
 * syn68k_common.cpp implements the syn68k API (callbacks, trap vectors,
 * interrupts, memory) once. What differs per core is how guest code runs
 * and where the core keeps its registers while it does; that is an engine.
 * One engine is chosen (syn68k_select_engine) before
 * initialize_68k_emulator and stays for the life of the process.
 *
 *   uae      MacPhoenix's UAE interpreter (syn68k_uae.cpp)
 *   musashi  Musashi (syn68k_musashi.cpp, src/cpu/musashi)
 *
 * Engines include their own core's headers; the two never meet in one
 * translation unit (both define m68k_execute and friends).
 */
#pragma once

#include <syn68k_public.h>

struct Syn68kEngine
{
    const char *name;
    /* Start the core: 68040, supervisor mode, interrupts enabled, VBR at
     * vbr. Fills cpu_state from the core's registers. */
    void (*init)(syn68k_addr_t vbr);
    /* Run from addr until the guest is back at the exit cell. Loads the
     * core from cpu_state first and stores it back after. Nested calls (a
     * callback running guest code) restore the outer PC. */
    void (*run_until_exit)(syn68k_addr_t addr);
    /* The guest PC while guest code runs (emulation_depth > 0). */
    syn68k_addr_t (*current_pc)(void);
    /* The core's own state for a process switch (beyond cpu_state). */
    size_t (*context_size)(void);
    void (*save_context)(void *dst);
    void (*restore_context)(const void *src);
    /* An interrupt may be pending: make the core look. */
    void (*note_interrupt)(void);
};

extern const Syn68kEngine syn68k_uae_engine;
extern const Syn68kEngine syn68k_musashi_engine;

/* Shared by the engines (syn68k_common.cpp). */

/* The callback page's opcodes. Both are illegal on a real 68k (MOVEQ needs
 * bit 8 clear); UAE knows them as EmulOps. */
static const uint16 SYN68K_OP_EXEC_RETURN = 0x7100;  /* leave run_until_exit */
static const uint16 SYN68K_OP_CALLBACK    = 0x7101;  /* dispatch callback by PC */

/* Run the callback whose cell is at pc. cpu_state must hold the guest
 * registers; returns where the guest continues. Aborts on a dead cell. */
syn68k_addr_t syn68k_dispatch_callback(syn68k_addr_t pc);
/* Highest interrupt level pending, or -1. */
int syn68k_highest_pending(void);
/* True inside the callback page (never a breakpoint). */
bool syn68k_in_callback_page(syn68k_addr_t pc);
/* Status register with the condition codes from cpu_state folded in. */
uint16 syn68k_full_sr(void);
/* Split a full status register into cpu_state.sr and the cc fields. */
void syn68k_set_full_sr(uint16 sr);

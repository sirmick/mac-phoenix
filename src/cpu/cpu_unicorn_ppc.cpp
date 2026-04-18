// Unicorn PPC backend (scaffold).
//
// Alternative PPC backend using Unicorn Engine's TCG-based PPC target instead
// of KPX's SheepShaver-derived interpreter. Coexists with KPX and is selected
// via --backend unicorn when --arch ppc is passed.
//
// See docs/ppc/UnicornPpcPlan.md for the full integration plan.
//
// Current status: stub — logs and exits. The actual CPU init, memory map,
// execute loop, EmulOp dispatch, and interrupt handshake are tracked by
// follow-up tasks (§3-§9 of the plan).

#include <cstdio>
#include <cstdint>
#include <cstdlib>

#include "platform.h"

extern "C" {

static bool uppc_cpu_init(void) {
    fprintf(stderr,
            "[Unicorn-PPC] Backend selected via --backend unicorn, but "
            "the CPU engine is not yet implemented. See "
            "docs/ppc/UnicornPpcPlan.md for the integration plan.\n");
    return false;
}

static void uppc_cpu_reset(void)                     {}
static void uppc_cpu_set_type(int, int)              {}

static int  uppc_cpu_execute_one(void)               { return 0; }
static void uppc_cpu_execute_fast(void)              {}
static void uppc_cpu_request_stop(void)              {}

static uint32_t uppc_cpu_get_pc(void)                { return 0; }
static uint16_t uppc_cpu_get_sr(void)                { return 0; }
static uint32_t uppc_cpu_get_dreg(int)               { return 0; }
static uint32_t uppc_cpu_get_areg(int)               { return 0; }

static uint32_t uppc_cpu_get_gpr(int)                { return 0; }
static void     uppc_cpu_set_gpr(int, uint32_t)     {}
static uint32_t uppc_cpu_get_cr(void)                { return 0; }
static uint32_t uppc_cpu_get_lr(void)                { return 0; }
static uint32_t uppc_cpu_get_ctr(void)               { return 0; }
static void     uppc_cpu_execute_ppc(uint32_t)       {}

static void uppc_cpu_trigger_interrupt(int)          {}
static void uppc_invoke_debug(void)                  {}

static void uppc_cpu_execute_68k_trap(uint16_t, struct M68kRegisters *) {}
static void uppc_cpu_execute_68k(uint32_t, struct M68kRegisters *)      {}

static void uppc_flush_code_cache(void)              {}

static uint8_t  uppc_mem_read_byte(uint32_t)          { return 0; }
static uint16_t uppc_mem_read_word(uint32_t)          { return 0; }
static uint32_t uppc_mem_read_long(uint32_t)          { return 0; }
static void     uppc_mem_write_byte(uint32_t, uint8_t)  {}
static void     uppc_mem_write_word(uint32_t, uint16_t) {}
static void     uppc_mem_write_long(uint32_t, uint32_t) {}

static uint16_t uppc_make_emulop(uint16_t op)        { return op; }

static void uppc_ppc_emulop_handler(void *, uint32_t, int)  {}
static void uppc_ppc_cursor_move(uint32_t, int, int)        {}

// Install: wire the (stub) Unicorn PPC backend into the Platform API.
void cpu_unicorn_ppc_install(Platform *p)
{
    p->cpu_name = "Unicorn-PPC (stub)";
    p->use_aline_emulops = false;  // PPC uses POWERPC_EMUL_OP (0x18xxxxxx)

    // Lifecycle
    p->cpu_init = uppc_cpu_init;
    p->cpu_reset = uppc_cpu_reset;
    p->cpu_set_type = uppc_cpu_set_type;

    // Execution
    p->cpu_execute_one = uppc_cpu_execute_one;
    p->cpu_execute_fast = uppc_cpu_execute_fast;
    p->cpu_request_stop = uppc_cpu_request_stop;

    // Generic state query
    p->cpu_get_pc = uppc_cpu_get_pc;
    p->cpu_get_sr = uppc_cpu_get_sr;
    p->cpu_get_dreg = uppc_cpu_get_dreg;
    p->cpu_get_areg = uppc_cpu_get_areg;

    // PPC-specific accessors
    p->cpu_get_gpr = uppc_cpu_get_gpr;
    p->cpu_set_gpr = uppc_cpu_set_gpr;
    p->cpu_get_cr = uppc_cpu_get_cr;
    p->cpu_get_lr = uppc_cpu_get_lr;
    p->cpu_get_ctr = uppc_cpu_get_ctr;
    p->cpu_execute_ppc = uppc_cpu_execute_ppc;

    // Interrupts + debug
    p->cpu_trigger_interrupt = uppc_cpu_trigger_interrupt;
    p->invoke_debug = uppc_invoke_debug;

    // 68k execution (from PPC context, via nanokernel)
    p->cpu_execute_68k_trap = uppc_cpu_execute_68k_trap;
    p->cpu_execute_68k = uppc_cpu_execute_68k;

    // Code cache
    p->flush_code_cache = uppc_flush_code_cache;

    // Memory (stub: all zeros)
    p->mem_read_byte = uppc_mem_read_byte;
    p->mem_read_word = uppc_mem_read_word;
    p->mem_read_long = uppc_mem_read_long;
    p->mem_write_byte = uppc_mem_write_byte;
    p->mem_write_word = uppc_mem_write_word;
    p->mem_write_long = uppc_mem_write_long;
    p->mem_mac_to_host = nullptr;
    p->mem_host_to_mac = nullptr;

    // EmulOp + trap + cursor
    p->make_emulop = uppc_make_emulop;
    p->m68k_emulop_handler = nullptr;
    p->ppc_emulop_handler = uppc_ppc_emulop_handler;
    p->trap_handler = nullptr;
    p->ppc_cursor_move = uppc_ppc_cursor_move;
}

} // extern "C"

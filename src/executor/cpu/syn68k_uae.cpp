/*
 * syn68k_uae.cpp - the UAE engine behind the syn68k facade.
 *
 * See syn68k_engine.h. Guest code runs in MacPhoenix's UAE interpreter;
 * the callback page's opcodes are UAE EmulOps (0x7100 leaves
 * m68k_execute(), 0x7101 lands in facade_emulop). Memory goes through
 * g_platform's identity hooks.
 *
 * Compiled with UAE's flags and include path (DIRECT_ADDRESSING=1).
 */
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "m68k.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "spcflags.h"
#include "platform.h"

#include "syn68k_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern bool quit_program;
extern uintptr MEMBaseDiff;

/* ---------------------------------------------------------------------- */
/* Register sync                                                          */
/* ---------------------------------------------------------------------- */

static void uae_to_cpu_state()
{
    for(int i = 0; i < 16; i++)
        cpu_state.regs[i].ul.n = regs.regs[i];
    MakeSR();
    cpu_state.sr = regs.sr & ~0x1F;
    cpu_state.ccc = GET_CFLG;
    cpu_state.ccv = GET_VFLG;
    cpu_state.ccn = GET_NFLG;
    cpu_state.ccx = GET_XFLG;
    cpu_state.ccnz = !GET_ZFLG;
    cpu_state.vbr = regs.vbr;
}

static void cpu_state_to_uae()
{
    for(int i = 0; i < 16; i++)
        regs.regs[i] = cpu_state.regs[i].ul.n;
    SET_CFLG(cpu_state.ccc != 0);
    SET_VFLG(cpu_state.ccv != 0);
    SET_NFLG(cpu_state.ccn != 0);
    SET_XFLG(cpu_state.ccx != 0);
    SET_ZFLG(cpu_state.ccnz == 0);
    /* Host code may change the interrupt mask or supervisor bits. */
    MakeSR();
    if((regs.sr & ~0x1F) != cpu_state.sr)
    {
        regs.sr = (regs.sr & 0x1F) | (cpu_state.sr & ~0x1F);
        MakeFromSR();
    }
}

/* ---------------------------------------------------------------------- */
/* Callbacks                                                              */
/* ---------------------------------------------------------------------- */

/* EmulOp hook: UAE calls this for 0x71xx (xx != 0), then advances PC by 2. */
static bool facade_emulop(uint16_t opcode, bool /*is_primary*/)
{
    uaecptr pc = m68k_getpc();
    if(opcode != SYN68K_OP_CALLBACK)
    {
        fprintf(stderr, "syn68k-uae: unexpected EmulOp %04x at %08x\n", opcode, pc);
        abort();
    }

    uae_to_cpu_state();
    syn68k_addr_t next = syn68k_dispatch_callback(pc);
    cpu_state_to_uae();

    /* UAE adds 2 after the EmulOp returns. */
    m68k_setpc(next - 2);
    fill_prefetch_0();
    return true;
}

/* ---------------------------------------------------------------------- */
/* Interrupts                                                             */
/* ---------------------------------------------------------------------- */

/* g_platform.m68k_poll_interrupts: polled from UAE's tick check. A level
 * that is still pending (masked, or not yet acknowledged) re-raises the
 * flag on every poll until a handler clears it. */
static void facade_poll_interrupts(void)
{
    if(syn68k_highest_pending() > 0)
        SPCFLAGS_SET(SPCFLAG_INT);
}

/* g_platform.m68k_intlev: level to deliver, or -1. */
static int facade_intlev(void)
{
    return syn68k_highest_pending();
}

static void uae_note_interrupt(void)
{
    SPCFLAGS_SET(SPCFLAG_INT);
}

/* ---------------------------------------------------------------------- */
/* Running guest code                                                     */
/* ---------------------------------------------------------------------- */

/* Instruction-at-a-time loop used while a debugger is attached. Matches
 * syn68k: before each instruction, if getNextBreakpoint(pc) == pc the
 * debugger is entered and returns where to continue. The callback page is
 * never a breakpoint. */
static void execute_with_debugger()
{
    for(;;)
    {
        if(quit_program)
            break;
        uaecptr pc = m68k_getpc();
        if(!syn68k_in_callback_page(pc)
           && syn68k_debugger_callbacks.getNextBreakpoint
           && syn68k_debugger_callbacks.getNextBreakpoint(pc) == pc)
        {
            uae_to_cpu_state();
            uaecptr next = syn68k_debugger_callbacks.debugger(pc);
            cpu_state_to_uae();
            if(next != pc)
            {
                m68k_setpc(next);
                fill_prefetch_0();
                continue;
            }
        }
        uae_u32 opcode = GET_OPCODE;
        (*cpufunctbl[opcode])(opcode);
        cpu_check_ticks();
        if(SPCFLAGS_TEST(SPCFLAG_ALL_BUT_EXEC_RETURN))
            m68k_do_specialties();
    }
}

static void uae_run_until_exit(syn68k_addr_t addr)
{
    bool outermost = emulation_depth == 0;
    uaecptr oldpc = outermost ? 0 : m68k_getpc();

    cpu_state_to_uae();
    m68k_setpc(addr);
    fill_prefetch_0();

    ++emulation_depth;
    /* UAE services a pending interrupt in the same specialties pass that
     * honours the exit request, so m68k_execute() can return with the guest
     * just diverted into an interrupt handler (PC at the handler, a frame on
     * A7, mask raised). Keep running until the guest is really back at the
     * exit cell; otherwise the host reads the frame as its result. */
    const uaecptr exit_pc = MAGIC_EXIT_EMULATOR_ADDRESS;
    do
    {
        quit_program = false;
        if(syn68k_debugger_callbacks.debugger && syn68k_debugger_callbacks.getNextBreakpoint)
            execute_with_debugger();
        else
            m68k_execute();
    } while(m68k_getpc() != exit_pc);
    quit_program = false;
    --emulation_depth;

    uae_to_cpu_state();
    if(!outermost)
    {
        m68k_setpc(oldpc);
        fill_prefetch_0();
    }
}

static syn68k_addr_t uae_current_pc(void)
{
    return m68k_getpc();
}

/* ---------------------------------------------------------------------- */
/* Process contexts                                                       */
/* ---------------------------------------------------------------------- */

namespace {
struct UaeContext
{
    regstruct uae_regs;
    flag_struct uae_flags;
    bool quit;
};
}

static size_t uae_context_size(void)
{
    return sizeof(UaeContext);
}

static void uae_save_context(void *context)
{
    UaeContext *c = (UaeContext *)context;
    c->uae_regs = regs;
    c->uae_flags = regflags;
    c->quit = quit_program;
}

static void uae_restore_context(const void *context)
{
    const UaeContext *c = (const UaeContext *)context;
    regs = c->uae_regs;
    regflags = c->uae_flags;
    quit_program = c->quit;
}

/* ---------------------------------------------------------------------- */
/* UAE memory hooks: identity, big-endian                                 */
/* ---------------------------------------------------------------------- */

static uint8_t mem_rb(uint32_t a) { return READUB(a); }
static uint16_t mem_rw(uint32_t a) { return READUW(a); }
static uint32_t mem_rl(uint32_t a) { return READUL(a); }
static void mem_wb(uint32_t a, uint8_t v) { WRITEUB(a, v); }
static void mem_ww(uint32_t a, uint16_t v) { WRITEUW(a, v); }
static void mem_wl(uint32_t a, uint32_t v) { WRITEUL(a, v); }
static uint8_t *mem_m2h(uint32_t a) { return (uint8_t *)(uintptr_t)a; }
static uint32_t mem_h2m(uint8_t *p) { return US_TO_SYN68K(p); }

/* ---------------------------------------------------------------------- */
/* Init                                                                   */
/* ---------------------------------------------------------------------- */

static void uae_init(syn68k_addr_t vbr)
{
    extern int CPUType, FPUType;
    CPUType = 4;   /* 68040 */
    FPUType = 0;   /* no FPU: Executor provides SANE natively */

    MEMBaseDiff = 0;

    g_platform.mem_read_byte = mem_rb;
    g_platform.mem_read_word = mem_rw;
    g_platform.mem_read_long = mem_rl;
    g_platform.mem_write_byte = mem_wb;
    g_platform.mem_write_word = mem_ww;
    g_platform.mem_write_long = mem_wl;
    g_platform.mem_mac_to_host = mem_m2h;
    g_platform.mem_host_to_mac = mem_h2m;
    g_platform.m68k_emulop_handler = facade_emulop;
    g_platform.m68k_intlev = facade_intlev;
    g_platform.m68k_poll_interrupts = facade_poll_interrupts;
    g_platform.trap_handler = nullptr;   /* real A-line/F-line exceptions */

    init_m68k();
    m68k_reset();

    /* Supervisor mode, interrupts enabled, like the Mac OS runs apps. */
    regs.s = 1;
    regs.m = 0;
    regs.intmask = 0;
    regs.vbr = vbr;
    MakeSR();
    uae_to_cpu_state();
}

const Syn68kEngine syn68k_uae_engine = {
    "uae",
    uae_init,
    uae_run_until_exit,
    uae_current_pc,
    uae_context_size,
    uae_save_context,
    uae_restore_context,
    uae_note_interrupt,
};

/*
 * syn68k_musashi.cpp - the Musashi engine behind the syn68k facade.
 *
 * See syn68k_engine.h. Musashi (src/cpu/musashi) runs as a 68040 in
 * supervisor mode. Memory goes through m68k_read/write_memory_*, which
 * Musashi calls for every access; for now they are the facade's identity
 * reads and writes.
 *
 * The callback page's opcodes ($7100, $7101) are illegal instructions to
 * Musashi; its illegal-instruction callback handles them. Musashi has
 * already stepped past the opcode by then, and REG_PPC holds its address.
 *
 * Interrupts: Musashi has a level-triggered interrupt line that it samples
 * when m68k_execute() starts and when the guest writes SR (RTE, MOVE to
 * SR). The line follows cpu_state.interrupt_pending[]: it is set before
 * every time slice and after every callback (an interrupt handler is a
 * callback that clears its level). Guest code runs in short time slices so
 * a pending interrupt waits at most one slice.
 */
#include "syn68k_engine.h"

#include "m68k.h"

#include <cstdio>
#include <cstdlib>

/* Cycles per m68k_execute() call: the worst-case interrupt latency. */
static const int SLICE_CYCLES = 4000;

/* ---------------------------------------------------------------------- */
/* Memory: Musashi calls these for every access                           */
/* ---------------------------------------------------------------------- */

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return READUB(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return READUW(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return READUL(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { WRITEUB(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { WRITEUW(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { WRITEUL(a, v); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return READUB(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return READUW(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return READUL(a); }
}

/* ---------------------------------------------------------------------- */
/* Register sync                                                          */
/* ---------------------------------------------------------------------- */

static void musashi_to_cpu_state()
{
    for(int i = 0; i < 16; i++)
        cpu_state.regs[i].ul.n = m68k_get_reg(nullptr, (m68k_register_t)(M68K_REG_D0 + i));
    syn68k_set_full_sr(m68k_get_reg(nullptr, M68K_REG_SR));
    cpu_state.vbr = m68k_get_reg(nullptr, M68K_REG_VBR);
}

/* The interrupt line follows the pending levels. Setting it never delivers
 * an interrupt by itself; Musashi samples it later. */
static void sync_irq()
{
    int level = syn68k_highest_pending();
    m68k_set_irq(level > 0 ? level : 0);
}

static void cpu_state_to_musashi()
{
    /* Writing SR samples the interrupt line, and this runs inside a
     * callback that is about to set the PC: lower the line first so no
     * interrupt is taken here. sync_irq() raises it again afterwards. */
    uint16 sr = syn68k_full_sr();
    if(m68k_get_reg(nullptr, M68K_REG_SR) != sr)
    {
        m68k_set_irq(0);
        m68k_set_reg(M68K_REG_SR, sr);
    }
    /* After SR: a supervisor-bit change swaps stack pointers, and A7 must
     * end up as cpu_state has it. */
    for(int i = 0; i < 16; i++)
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), cpu_state.regs[i].ul.n);
}

/* ---------------------------------------------------------------------- */
/* Callbacks                                                              */
/* ---------------------------------------------------------------------- */

static int musashi_illegal(int opcode)
{
    syn68k_addr_t pc = m68k_get_reg(nullptr, M68K_REG_PPC);
    if(opcode == SYN68K_OP_CALLBACK)
    {
        musashi_to_cpu_state();
        syn68k_addr_t next = syn68k_dispatch_callback(pc);
        cpu_state_to_musashi();
        m68k_set_reg(M68K_REG_PC, next);
        sync_irq();
        return 1;
    }
    if(opcode == SYN68K_OP_EXEC_RETURN)
    {
        /* Stay on the exit cell; run_until_exit sees it and returns. */
        m68k_set_reg(M68K_REG_PC, pc);
        m68k_end_timeslice();
        return 1;
    }
    if((opcode & 0xFF00) == 0x7100)
    {
        fprintf(stderr, "syn68k-musashi: unexpected callback opcode %04x at %08x\n", opcode, pc);
        abort();
    }
    return 0;  /* a real illegal instruction: take the exception */
}

/* ---------------------------------------------------------------------- */
/* Running guest code                                                     */
/* ---------------------------------------------------------------------- */

static syn68k_addr_t musashi_current_pc(void)
{
    return m68k_get_reg(nullptr, M68K_REG_PC);
}

/* One instruction at a time while a debugger is attached. Matches syn68k:
 * before each instruction, if getNextBreakpoint(pc) == pc the debugger is
 * entered and returns where to continue. The callback page is never a
 * breakpoint. */
static void step_with_debugger()
{
    syn68k_addr_t pc = musashi_current_pc();
    if(!syn68k_in_callback_page(pc)
       && syn68k_debugger_callbacks.getNextBreakpoint(pc) == pc)
    {
        musashi_to_cpu_state();
        syn68k_addr_t next = syn68k_debugger_callbacks.debugger(pc);
        cpu_state_to_musashi();
        if(next != pc)
        {
            m68k_set_reg(M68K_REG_PC, next);
            return;
        }
    }
    m68k_execute(1);
}

static void musashi_run_until_exit(syn68k_addr_t addr)
{
    bool outermost = emulation_depth == 0;
    syn68k_addr_t oldpc = outermost ? 0 : musashi_current_pc();

    cpu_state_to_musashi();
    m68k_set_reg(M68K_REG_PC, addr);

    /* m68k_execute()'s cycle count is global, so a nested run (a callback
     * running guest code) ends the outer time slice early. That only costs
     * an extra trip round this loop, which checks the PC, not the count. */
    ++emulation_depth;
    const syn68k_addr_t exit_pc = MAGIC_EXIT_EMULATOR_ADDRESS;
    while(musashi_current_pc() != exit_pc)
    {
        sync_irq();
        if(syn68k_debugger_callbacks.debugger && syn68k_debugger_callbacks.getNextBreakpoint)
            step_with_debugger();
        else
            m68k_execute(SLICE_CYCLES);
    }
    --emulation_depth;

    musashi_to_cpu_state();
    if(!outermost)
        m68k_set_reg(M68K_REG_PC, oldpc);
}

/* ---------------------------------------------------------------------- */
/* Process contexts                                                       */
/* ---------------------------------------------------------------------- */

static size_t musashi_context_size(void)
{
    return m68k_context_size();
}

static void musashi_save_context(void *context)
{
    m68k_get_context(context);
}

static void musashi_restore_context(const void *context)
{
    m68k_set_context(const_cast<void *>(context));
    /* The saved interrupt line is stale. */
    sync_irq();
}

static void musashi_note_interrupt(void)
{
    /* The line is resynced before every time slice and after every
     * callback; nothing to poke. */
}

/* ---------------------------------------------------------------------- */
/* Init                                                                   */
/* ---------------------------------------------------------------------- */

static void musashi_init(syn68k_addr_t vbr)
{
    m68k_set_cpu_type(M68K_CPU_TYPE_68040);
    m68k_init();
    m68k_set_illg_instr_callback(musashi_illegal);
    m68k_pulse_reset();

    /* Supervisor mode, interrupts enabled, like the Mac OS runs apps. */
    m68k_set_reg(M68K_REG_SR, 0x2000);
    m68k_set_reg(M68K_REG_VBR, vbr);
    m68k_set_reg(M68K_REG_CACR, 0);
    musashi_to_cpu_state();
}

const Syn68kEngine syn68k_musashi_engine = {
    "musashi",
    musashi_init,
    musashi_run_until_exit,
    musashi_current_pc,
    musashi_context_size,
    musashi_save_context,
    musashi_restore_context,
    musashi_note_interrupt,
};

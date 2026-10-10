/*
 *  cpu_ppc_mame.cpp - the SheepShaver machine side on MAME's PowerPC core
 *  (cpu::Core "mame-ppc", src/cpu/mame). docs/cpu/PLAN.md, C2b.
 *
 *  Everything Mac-specific stays in kpx_shared (ROM patches, EmulOps,
 *  thunks, drivers, KernelData, SheepMem); this file is the Platform table
 *  cpu_ppc_kpx.cpp fills from Kheperix, filled from a cpu::Core instead.
 *  The two differ only in how the CPU is driven:
 *
 *    SHEEP opcodes (primary opcode 6)   cpu::Host::host_op()
 *    spcflags / HandleInterrupt         request_attention() -> attention()
 *    execute(entry) until EXEC_RETURN   a nested run() ended by the host op
 *    gpr()/pc()/lr()/cr() references    reg()/set_reg()
 *    register save around Execute68k    save_context()/restore_context()
 *
 *  Addressing is identity, as under Kheperix (REAL_ADDRESSING): the
 *  GuestMemory is one window at host 0, so unmapped guest addresses fault
 *  on the host and the SIGSEGV handler skips the access, which is what the
 *  nanokernel's boot-time probes expect. One base offset comes later
 *  (PLAN.md, C2e).
 *
 *  Original machine logic: SheepShaver (C) 1997-2008 Christian Bauer and
 *  Marc Hellwig, GPL v2+.
 */
#include "sysdeps.h"
#include <sys/mman.h>
#include <unistd.h>
#include "kpx_cpu_emulation.h"
#include "main.h"
#include "xlowmem.h"
#include "emul_op.h"
#include "thunks.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "video.h"
#include "timer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>
#include <vector>

#include "../../common/include/sigsegv.h"
#include "../../common/include/ppc_boundary_trace.h"
extern "C" {
#include "platform.h"
}
#include "cpu_core.h"

extern "C" unsigned int boot_progress_checkloads(void);
extern "C" void boot_progress_export_cursor_to_ipc(void);
extern "C" void boot_progress_export_app_to_ipc(void);
extern "C" void boot_progress_export_mac_state(void);
extern "C" void check_load_invoc(uint32 type, int16 id, uint32 h);
extern "C" void named_check_load_invoc(uint32 type, uint32 name, uint32 h);
extern bool tick_inhibit;
extern void video_ppc_init_webrtc_null(void);
extern uintptr SignalStackBase(void);
bool ppc_native_op_pure(uint32 selector, uint32 *gprs);   // native_ops_ppc.cpp
extern int64 CPUClockSpeed;
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);
extern uint64_t ppc_insn_counter;   /* Kheperix's; Microseconds() reads it (ppc_memory.cpp) */

using namespace ppc;
namespace R = cpu::ppc;

namespace {

/* SHEEP opcode fields (cpu_ppc_kpx.cpp): FN at bit 19, NATIVE_OP bits
 * 20-25, EMUL_OP bits 26-31, IBM bit numbering. */
inline uint32 sheep_fn(uint32 op) { return (op >> 12) & 1; }
inline uint32 sheep_native_op(uint32 op) { return (op >> 6) & 0x3f; }
inline uint32 sheep_emul_op(uint32 op) { return op & 0x3f; }

const uint32 POWERPC_EXEC_RETURN = 0x18000001;   /* SHEEP, EXEC_RETURN */

#define INTERRUPTS_IN_EMUL_OP_MODE 1
#define INTERRUPTS_IN_NATIVE_MODE 1

cpu::GuestMemory g_memory;
cpu::Core *g_core;
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_tick_running{false};
bool g_exec_return;   /* the innermost run() ended on EXEC_RETURN */

/* Register helpers. */
inline uint32 gpr(int n) { return g_core->reg(R::GPR0 + n); }
inline void set_gpr(int n, uint32 v) { g_core->set_reg(R::GPR0 + n, v); }
inline uint32 get_cr() { return g_core->reg(R::CR); }
inline void set_cr(uint32 v) { g_core->set_reg(R::CR, v); }
inline uint32 get_xer() { return g_core->reg(R::XER); }
inline void set_xer(uint32 v) { g_core->set_reg(R::XER, v); }
inline uint32 get_lr() { return g_core->reg(R::LR); }
inline void set_lr(uint32 v) { g_core->set_reg(R::LR, v); }
inline uint32 get_ctr() { return g_core->reg(R::CTR); }
inline void set_ctr(uint32 v) { g_core->set_reg(R::CTR, v); }

/* Run guest code at entry until the EXEC_RETURN host op (SheepShaver's
 * execute(entry)). A stop request unwinds every nesting level. */
void run_until_exec_return(uint32 entry)
{
    g_core->set_pc(entry);
    for(;;)
    {
        cpu::StopReason why = g_core->run();
        if(g_exec_return)
        {
            g_exec_return = false;
            return;
        }
        if(why == cpu::StopReason::Requested && g_stop_requested)
            return;
    }
}

void execute_ppc(uint32 entry)
{
    uint32 saved_lr = get_lr();
    SheepVar32 trampoline(POWERPC_EXEC_RETURN);
    set_lr(trampoline.addr());
    run_until_exec_return(entry);
    set_lr(saved_lr);
}

/* Call a Mac OS routine by TVector (SheepShaver's execute_macos_code). */
uint32 execute_macos_code(uint32 tvect, int nargs, uint32 const *args)
{
    uint32 saved_pc = g_core->pc();
    uint32 saved_lr = get_lr();
    uint32 saved_ctr = get_ctr();

    SheepVar32 trampoline(POWERPC_EXEC_RETURN);
    set_lr(trampoline.addr());

    set_gpr(1, gpr(1) - 64);
    uint32 proc = ReadMacInt32(tvect);
    uint32 toc = ReadMacInt32(tvect + 4);

    uint32 regs[8];
    regs[0] = gpr(2);
    for(int i = 0; i < nargs; i++)
        regs[i + 1] = gpr(i + 3);

    set_gpr(2, toc);
    for(int i = 0; i < nargs; i++)
        set_gpr(i + 3, args[i]);
    run_until_exec_return(proc);
    uint32 retval = gpr(3);

    for(int i = 0; i <= nargs; i++)
        set_gpr(i + 2, regs[i]);

    set_gpr(1, gpr(1) + 64);
    g_core->set_pc(saved_pc);
    set_lr(saved_lr);
    set_ctr(saved_ctr);
    return retval;
}

/* The GET_RESOURCE family: run the original ROM routine, then CheckLoad. */
void get_resource(uint32 old_get_resource)
{
    uint32 type = gpr(3);
    int16 id = gpr(4);

    set_gpr(1, gpr(1) - 56);
    execute_ppc(old_get_resource);

    uint32 handle = gpr(3);
    check_load_invoc(type, id, handle);
    set_gpr(3, handle);

    set_gpr(1, gpr(1) + 56);
}

unsigned g_native_counts[64];
unsigned g_emulop_counts[64];

void execute_native_op(uint32 selector)
{
    if(selector < 64)
        g_native_counts[selector]++;
    uint32 gprs[32];
    for(int i = 0; i < 32; i++)
        gprs[i] = gpr(i);
    if(ppc_native_op_pure(selector, gprs))
    {
        set_gpr(3, gprs[3]);   /* the only register the table writes */
        return;
    }
    switch(selector)
    {
        case NATIVE_GET_RESOURCE: get_resource(ReadMacInt32(XLM_GET_RESOURCE)); break;
        case NATIVE_GET_1_RESOURCE: get_resource(ReadMacInt32(XLM_GET_1_RESOURCE)); break;
        case NATIVE_GET_IND_RESOURCE: get_resource(ReadMacInt32(XLM_GET_IND_RESOURCE)); break;
        case NATIVE_GET_1_IND_RESOURCE: get_resource(ReadMacInt32(XLM_GET_1_IND_RESOURCE)); break;
        case NATIVE_R_GET_RESOURCE: get_resource(ReadMacInt32(XLM_R_GET_RESOURCE)); break;
        case NATIVE_GET_NAMED_RESOURCE: get_resource(ReadMacInt32(XLM_GET_NAMED_RESOURCE)); break;
        case NATIVE_GET_1_NAMED_RESOURCE: get_resource(ReadMacInt32(XLM_GET_1_NAMED_RESOURCE)); break;
    }
}

/* An EmulOp from 68k code: the 68k register file lives in r8-r24 (D0-D7
 * in r8-r15, A0-A6 in r16-r22, A7 in r1, PC in r24), as the nanokernel's
 * emulator keeps it. */
void execute_emul_op(uint32 emul_op)
{
    if(!g_platform.ppc_emulop_handler)
    {
        fprintf(stderr, "FATAL: g_platform.ppc_emulop_handler not set\n");
        abort();
    }
    if(emul_op < 64)
        g_emulop_counts[emul_op]++;
    /* Microseconds() on this machine is a virtual clock off Kheperix's
     * instruction counter (ppc_memory.cpp); keep it moving from the core's
     * cycle count, or a guest waiting on Microseconds spins forever (Mac OS
     * 9.0.4's boot did, at the progress bar). */
    ppc_insn_counter = g_core->cycles();
    M68kRegisters r68;
    WriteMacInt32(XLM_68K_R25, gpr(25));
    WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);
    for(int i = 0; i < 8; i++)
        r68.d[i] = gpr(8 + i);
    for(int i = 0; i < 7; i++)
        r68.a[i] = gpr(16 + i);
    r68.a[7] = gpr(1);
    uint32 saved_cr = get_cr() & 0xff9fffff;
    uint32 saved_xer = get_xer();
    {
        PpcBoundaryState ts;
        ts.pc = g_core->pc() - 4;
        ts.selector = emul_op;
        ts.cr = get_cr();
        ts.xer = saved_xer;
        ts.lr = get_lr();
        ts.ctr = get_ctr();
        for(int i = 0; i < 32; ++i) ts.gpr[i] = gpr(i);
        ppc_trace_emul_op(ts);
    }
    g_platform.ppc_emulop_handler(&r68, gpr(24), emul_op);
    set_cr(saved_cr);
    set_xer(saved_xer);
    for(int i = 0; i < 8; i++)
        set_gpr(8 + i, r68.d[i]);
    for(int i = 0; i < 7; i++)
        set_gpr(16 + i, r68.a[i]);
    set_gpr(1, r68.a[7]);
    WriteMacInt32(XLM_RUN_MODE, MODE_68K);
    {
        PpcBoundaryState ts;
        ts.pc = g_core->pc() - 4;
        ts.selector = emul_op;
        ts.cr = get_cr();
        ts.xer = get_xer();
        ts.lr = get_lr();
        ts.ctr = get_ctr();
        for(int i = 0; i < 32; ++i) ts.gpr[i] = gpr(i);
        ppc_trace_emul_op_post(ts);
    }
}

/* ---- C3a: the 68k side on a host Musashi (MACEMU_MAMEPPC_68K=1) ----
 *
 * execute_68k() runs the routine on a mame-68k core over the same guest
 * memory. The ROM's EmulOp words ($FE43+n) and the EXEC_RETURN word
 * ($FE41) are handled here as they would be by the nanokernel's emulator.
 * The first Mixed Mode switch ($AAFE) hands the rest of the routine back
 * to that emulator: the 68k state goes into the PowerPC registers in the
 * emulator's convention and the emulator's own handler for $AAFE runs,
 * as if it had dispatched the word itself. docs/cpu/PLAN.md, C3. */
namespace m68k = cpu::m68k;
int use_host_m68k();
cpu::Core *g_m68k;
bool g_m68k_exec_return;   /* the routine returned (EXEC_RETURN word) */
bool g_m68k_handoff;       /* an A-line word: continue on the nanokernel's emulator */
uint32 g_m68k_handoff_op;  /* ... from its slot for this word */
bool g_m68k_active;        /* a Musashi run is in progress (tick trace) */
unsigned g_m68k_runs, g_m68k_handoffs, g_m68k_emulops;

struct M68kHost final : cpu::Host
{
    OpResult host_op(cpu::Core &core, uint32_t opcode, uint32_t op_pc) override
    {
        (void)core; (void)op_pc;
        fprintf(stderr, "[mame-ppc] 68k: unexpected host op %04x\n", opcode);
        return OpResult::Continue;
    }

    bool trap(cpu::Core &core, uint32_t opcode, uint32_t op_pc) override
    {
        if(opcode == M68K_EXEC_RETURN)
        {
            g_m68k_exec_return = true;
            core.request_stop();
            return true;
        }
        if(opcode == 0xfe40)   /* EMUL_RETURN */
        {
            QuitEmulator();
            return true;
        }
        if(opcode >= M68K_EMUL_BREAK && opcode < 0xff00)
        {
            g_m68k_emulops++;
            M68kRegisters r68;
            for(int i = 0; i < 8; i++)
            {
                r68.d[i] = core.reg(m68k::D0 + i);
                r68.a[i] = core.reg(m68k::A0 + i);
            }
            /* In the nanokernel's emulator r1 is A7 at all times, and a
             * handler that calls Execute68k builds its frame from r1. */
            set_gpr(1, r68.a[7]);
            WriteMacInt32(XLM_68K_R25, core.reg(m68k::SR) >> 8);
            WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);
            ppc::EmulOp(&r68, op_pc + 2, opcode - M68K_EMUL_BREAK);
            WriteMacInt32(XLM_RUN_MODE, MODE_68K);
            for(int i = 0; i < 8; i++)
            {
                core.set_reg(m68k::D0 + i, r68.d[i]);
                core.set_reg(m68k::A0 + i, r68.a[i]);
            }
            set_gpr(1, r68.a[7]);
            return true;
        }
        if(opcode == 0xaafe || ((opcode & 0xf000) == 0xa000 && use_host_m68k() == 1))
        {
            /* _MixedModeMagic, and (mode 1) every other A-line word too:
             * hand the routine back to the nanokernel's emulator from its
             * own slot for the word. Mode 2 lets Musashi take A-line traps
             * through the 68k vector at $28 (the ROM's own dispatcher). */
            g_m68k_handoff_op = opcode;
            g_m68k_handoff = true;
            core.request_stop();
            return true;
        }
        return false;   /* the 68k vector */
    }
} g_m68k_host;

/* MACEMU_MAMEPPC_68K: 0 off; 1 hand every A-line word to the emulator;
 * 2 Musashi takes A-line traps through the 68k vectors ($AAFE still
 * hands off). */
int use_host_m68k()
{
    static const int on = []() {
        const char *e = std::getenv("MACEMU_MAMEPPC_68K");
        return (e && *e) ? atoi(e) : 0;
    }();
    return on;
}

/* The emulator's register convention at a dispatch slot, from Musashi's
 * state: D0-D7 in r8-r15, A0-A6 in r16-r22, A7 in r1, PC (past the
 * opcode) in r24, the sign-extended next word in r27, the SR's high byte
 * in r25, supervisor as CR field 2 SO, r29 the slot. */
void export_m68k_to_emulator(uint32 opcode, uint32 pc)
{
    for(int i = 0; i < 8; i++)
        set_gpr(8 + i, g_m68k->reg(m68k::D0 + i));
    for(int i = 0; i < 7; i++)
        set_gpr(16 + i, g_m68k->reg(m68k::A0 + i));
    set_gpr(1, g_m68k->reg(m68k::A7));
    uint32 sr = g_m68k->reg(m68k::SR);
    set_gpr(0, 0);
    set_gpr(23, 0);
    set_gpr(24, pc);
    set_gpr(25, sr >> 8);
    WriteMacInt32(XLM_68K_R25, sr >> 8);
    set_gpr(26, 0);
    set_gpr(27, (uint32)(int32)(int16)ReadMacInt16(pc));
    set_gpr(28, 0);
    set_gpr(29, ReadMacInt32(KERNEL_DATA_BASE + 0x1074) + opcode * 8);
    set_gpr(30, ReadMacInt32(KERNEL_DATA_BASE + 0x1078));
    set_gpr(31, KernelDataAddr + 0x1000);
    set_cr((sr & 0x2000) ? 0x00100000 : 0);
}

bool execute_68k_musashi(uint32 entry, M68kRegisters *r)
{
    if(!g_m68k)
    {
        cpu::Config config;
        config.arch = cpu::Arch::M68K;
        config.model = 68040;
        config.fpu = false;   /* the nanokernel's emulator is a 68LC040 */
        config.mmu = false;
        g_m68k = cpu::create("mame-68k", config, g_memory, g_m68k_host).release();
        if(!g_m68k)
        {
            fprintf(stderr, "[mame-ppc] cannot create the mame-68k core; 68k stays on the emulator\n");
            return false;
        }
        fprintf(stderr, "[mame-ppc] 68k routines run on %s (mode %d); 68k vectors: $10=%08x $28=%08x $2C=%08x\n",
                g_m68k->name(), use_host_m68k(), ReadMacInt32(0x10), ReadMacInt32(0x28), ReadMacInt32(0x2c));
    }
    g_m68k_runs++;
    static int depth;
    static const bool s_trace = []() {
        const char *e = std::getenv("MACEMU_MAMEPPC_TRACE");
        return e && *e && *e != '0';
    }();

    /* Both cores' state: this nests (an EmulOp run from here may call
     * Execute68k again), and the outer Musashi instruction must find its
     * registers and PC as it left them. The flags are per level too. */
    std::vector<uint8_t> ctx(g_core->context_size());
    g_core->save_context(ctx.data());
    std::vector<uint8_t> m68ctx(g_m68k->context_size());
    g_m68k->save_context(m68ctx.data());
    bool const outer_exec_return = g_m68k_exec_return, outer_handoff = g_m68k_handoff;
    depth++;

    /* The MacOS stack frame and the return address, on the shared stack. */
    uint32 sp = gpr(1);
    uint32 a7 = sp - 56;
    WriteMacInt32(a7, sp);
    a7 -= 4;
    WriteMacInt32(a7, XLM_EXEC_RETURN_OPCODE);

    for(int i = 0; i < 8; i++)
        g_m68k->set_reg(m68k::D0 + i, r->d[i]);
    for(int i = 0; i < 7; i++)
        g_m68k->set_reg(m68k::A0 + i, r->a[i]);
    g_m68k->set_reg(m68k::SR, (ReadMacInt32(XLM_68K_R25) << 8) & 0xff00 | 0x2000);
    g_m68k->set_reg(m68k::A7, a7);
    g_m68k->set_reg(m68k::VBR, 0);
    g_m68k->invalidate(0, 0xFFFFFFFFu);
    g_m68k->set_pc(entry);
    WriteMacInt32(XLM_RUN_MODE, MODE_68K);

    g_m68k_exec_return = false;
    g_m68k_handoff = false;
    g_m68k_active = true;
    for(;;)
    {
        cpu::StopReason why = g_m68k->run();
        if(g_m68k_exec_return || g_m68k_handoff)
            break;
        if(why == cpu::StopReason::Requested && g_stop_requested)
            break;
    }

    g_m68k_active = false;
    if(g_m68k_handoff)
    {
        /* The rest of the routine on the nanokernel's emulator, from its
         * own handler for $AAFE; it ends at the EXEC_RETURN word as before. */
        g_m68k_handoffs++;
        /* Where the emulator keeps the CCR is not known, so let it set its
         * own: enter it at a thunk that loads the CCR from an immediate and
         * jumps to the word Musashi stopped at, which it then dispatches
         * itself.  move #ccr,ccr ; jmp word.l */
        uint32 word_pc = g_m68k->pc() - 2;
        SheepVar thunk(12);
        WriteMacInt16(thunk.addr() + 0, 0x44fc);
        WriteMacInt16(thunk.addr() + 2, g_m68k->reg(m68k::SR) & 0xff);
        WriteMacInt16(thunk.addr() + 4, 0x4ef9);
        WriteMacInt32(thunk.addr() + 6, word_pc);
        export_m68k_to_emulator(0x44fc, thunk.addr() + 2);
        run_until_exec_return(gpr(29));
        WriteMacInt32(XLM_68K_R25, gpr(25));
        for(int i = 0; i < 8; i++)
            r->d[i] = gpr(8 + i);
        for(int i = 0; i < 7; i++)
            r->a[i] = gpr(16 + i);
    }
    else
    {
        WriteMacInt32(XLM_68K_R25, g_m68k->reg(m68k::SR) >> 8);
        for(int i = 0; i < 8; i++)
            r->d[i] = g_m68k->reg(m68k::D0 + i);
        for(int i = 0; i < 7; i++)
            r->a[i] = g_m68k->reg(m68k::A0 + i);
    }
    if(s_trace && g_m68k_runs <= 400)
        fprintf(stderr, "[mame-ppc] 68k run %u depth %d entry=%08x: %s (68k pc %08x) d0=%08x d1=%08x a0=%08x a1=%08x\n",
                g_m68k_runs, depth, entry, g_m68k_handoff ? "handoff" : g_m68k_exec_return ? "returned" : "stopped",
                g_m68k->pc(), r->d[0], r->d[1], r->a[0], r->a[1]);
    WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);
    g_core->restore_context(ctx.data());
    g_m68k->restore_context(m68ctx.data());
    g_m68k_exec_return = outer_exec_return;
    g_m68k_handoff = outer_handoff;
    depth--;
    return true;
}

/* Run a 68k routine on the nanokernel's 68k emulator (SheepShaver's
 * execute_68k): hand it the 68k registers in the emulator's PowerPC
 * register convention, push the EXEC_RETURN return address, enter the
 * emulator at the dispatch slot of the first opcode. Everything the
 * PowerPC side had is restored from a context afterwards. */
void execute_68k(uint32 entry, M68kRegisters *r)
{
    if(ReadMacInt32(XLM_RUN_MODE) != MODE_EMUL_OP)
        fprintf(stderr, "FATAL: Execute68k() not called from EMUL_OP mode\n");
    if(use_host_m68k() && execute_68k_musashi(entry, r))
        return;

    std::vector<uint8_t> ctx(g_core->context_size());
    g_core->save_context(ctx.data());

    /* MacOS stack frame */
    uint32 sp = gpr(1);
    set_gpr(1, sp - 56);
    WriteMacInt32(gpr(1), sp);

    /* Registers for the 68k emulator */
    set_cr(0x00100000);   /* CR field 2, SO: supervisor mode */
    for(int i = 0; i < 8; i++)
        set_gpr(8 + i, r->d[i]);
    for(int i = 0; i < 7; i++)
        set_gpr(16 + i, r->a[i]);
    set_gpr(23, 0);
    set_gpr(24, entry);
    set_gpr(25, ReadMacInt32(XLM_68K_R25));              /* MSB of SR */
    set_gpr(26, 0);
    set_gpr(28, 0);                                      /* VBR */
    set_gpr(29, ReadMacInt32(KERNEL_DATA_BASE + 0x1074)); /* opcode table */
    set_gpr(30, ReadMacInt32(KERNEL_DATA_BASE + 0x1078)); /* emulator */
    set_gpr(31, KernelDataAddr + 0x1000);

    /* Return address: the EXEC_RETURN 68k opcode */
    set_gpr(1, gpr(1) - 4);
    WriteMacInt32(gpr(1), XLM_EXEC_RETURN_OPCODE);

    WriteMacInt32(XLM_RUN_MODE, MODE_68K);
    set_gpr(0, 0);

    /* Dispatch the first opcode as the emulator's loop would */
    uint32 opcode = ReadMacInt16(gpr(24));
    set_gpr(24, gpr(24) + 2);
    set_gpr(27, (int32)(int16)ReadMacInt16(gpr(24)));
    set_gpr(29, gpr(29) + opcode * 8);

    run_until_exec_return(gpr(29));

    WriteMacInt32(XLM_68K_R25, gpr(25));
    WriteMacInt32(XLM_RUN_MODE, MODE_EMUL_OP);

    for(int i = 0; i < 8; i++)
        r->d[i] = gpr(8 + i);
    for(int i = 0; i < 7; i++)
        r->a[i] = gpr(16 + i);

    /* Everything the PowerPC side had: r13-r31, f14-f31, pc, lr, ctr, cr,
     * the stack pointer (the frame goes with it). */
    g_core->restore_context(ctx.data());
}

/* The nanokernel's interrupt entry (SheepShaver's interrupt()), run on
 * the interrupt stack with the kernel's register conventions. */
void nanokernel_interrupt(uint32 entry)
{
    uint32 saved_pc = g_core->pc();
    uint32 saved_lr = get_lr();
    uint32 saved_ctr = get_ctr();
    uint32 saved_sp = gpr(1);

    set_gpr(1, SignalStackBase() - 64);

    SheepVar32 trampoline(POWERPC_EXEC_RETURN);

    WriteMacInt32(KERNEL_DATA_BASE + 0x004, gpr(1));
    WriteMacInt32(KERNEL_DATA_BASE + 0x018, gpr(6));

    set_gpr(6, ReadMacInt32(KERNEL_DATA_BASE + 0x65c));
    assert(gpr(6) != 0);
    WriteMacInt32(gpr(6) + 0x13c, gpr(7));
    WriteMacInt32(gpr(6) + 0x144, gpr(8));
    WriteMacInt32(gpr(6) + 0x14c, gpr(9));
    WriteMacInt32(gpr(6) + 0x154, gpr(10));
    WriteMacInt32(gpr(6) + 0x15c, gpr(11));
    WriteMacInt32(gpr(6) + 0x164, gpr(12));
    WriteMacInt32(gpr(6) + 0x16c, gpr(13));

    set_gpr(1, KernelDataAddr);
    set_gpr(7, ReadMacInt32(KERNEL_DATA_BASE + 0x660));
    set_gpr(8, 0);
    set_gpr(10, trampoline.addr());
    set_gpr(12, trampoline.addr());
    set_gpr(13, get_cr());

    /* rlwimi r7,r7,8,0,0 with CR0 recorded */
    uint32 r7 = gpr(7);
    uint32 result = ((r7 << 8) | (r7 >> 24)) & 0x80000000u;
    result |= r7 & 0x7fffffffu;
    uint32 cr0 = ((int32)result < 0) ? 8 : (result == 0 ? 2 : 4);
    cr0 |= (get_xer() >> 31) & 1;
    set_cr((get_cr() & 0x0fffffffu) | (cr0 << 28));
    set_gpr(7, result);

    set_gpr(11, 0xf072);
    set_cr((0xf072 & 0x0fff0000) | (get_cr() & ~0x0fff0000));

    if(ppc_trace_stream_())
        fprintf(ppc_trace_stream_(), "%08llu IRQ    entry=%08x saved_pc=%08x saved_cr=%08x\n",
                (unsigned long long)ppc_trace_seq_(), entry, saved_pc, gpr(13));

    run_until_exec_return(entry);

    g_core->set_pc(saved_pc);
    set_lr(saved_lr);
    set_ctr(saved_ctr);
    set_gpr(1, saved_sp);
}

/* HandleInterrupt: on the CPU thread, between instructions. */
void handle_interrupt()
{
    if(int32(ReadMacInt32(XLM_IRQ_NEST)) > 0)
        return;

    uint32 mode = ReadMacInt32(XLM_RUN_MODE);
    switch(mode)
    {
        case MODE_68K:
        {
            uint32 or_mask = ReadMacInt32(KERNEL_DATA_BASE + 0x674);
            uint32 cr_before = get_cr();
            WriteMacInt16(ReadMacInt32(KERNEL_DATA_BASE + 0x67c), 1);
            set_cr(cr_before | or_mask);
            if(ppc_trace_stream_())
                fprintf(ppc_trace_stream_(), "%08llu MODE68K cr_before=%08x or_mask=%08x cr_after=%08x\n",
                        (unsigned long long)ppc_trace_seq_(), cr_before, or_mask, get_cr());
            break;
        }

#if INTERRUPTS_IN_NATIVE_MODE
        case MODE_NATIVE:
            if(gpr(1) != KernelDataAddr)
            {
                WriteMacInt16(ReadMacInt32(KERNEL_DATA_BASE + 0x67c), 1);
                WriteMacInt32(ReadMacInt32(KERNEL_DATA_BASE + 0x658) + 0xdc,
                              ReadMacInt32(ReadMacInt32(KERNEL_DATA_BASE + 0x658) + 0xdc)
                                  | ReadMacInt32(KERNEL_DATA_BASE + 0x674));
                DisableInterrupt();
                if(ROMType == ROMTYPE_NEWWORLD)
                    nanokernel_interrupt(ROMBase + 0x312b1c);
                else
                    nanokernel_interrupt(ROMBase + 0x312a3c);
            }
            break;
#endif

#if INTERRUPTS_IN_EMUL_OP_MODE
        case MODE_EMUL_OP:
            if((ReadMacInt32(XLM_68K_R25) & 7) == 0)
            {
                M68kRegisters r;
                uint32 old_r25 = ReadMacInt32(XLM_68K_R25);
                WriteMacInt32(XLM_68K_R25, 0x21);
                static const uint8 proc_template[] = {
                    0x3f, 0x3c, 0x00, 0x00,
                    0x48, 0x7a, 0x00, 0x0a,
                    0x40, 0xe7,
                    0x20, 0x78, 0x00, 0x064,
                    0x4e, 0xd0,
                    M68K_RTS >> 8, M68K_RTS & 0xff
                };
                BUILD_SHEEPSHAVER_PROCEDURE(proc);
                execute_68k(proc, &r);
                WriteMacInt32(XLM_68K_R25, old_r25);
            }
            break;
#endif
    }
}

/* The core's host. */
struct MacHost final : cpu::Host
{
    OpResult host_op(cpu::Core &core, uint32_t opcode, uint32_t op_pc) override
    {
        static int logged;
        static const bool s_trace = []() {
            const char *e = std::getenv("MACEMU_MAMEPPC_TRACE");
            return e && *e && *e != '0';
        }();
        if(s_trace && logged < 200)
        {
            logged++;
            fprintf(stderr, "[mame-ppc] host op %08x at %08x (sel %u, native %u)\n", opcode, op_pc,
                    sheep_emul_op(opcode), sheep_native_op(opcode));
        }
        switch(sheep_emul_op(opcode))
        {
            case 0: /* EMUL_RETURN */
                QuitEmulator();
                return OpResult::Stop;
            case 1: /* EXEC_RETURN */
                core.set_pc(op_pc);
                g_exec_return = true;
                return OpResult::Stop;
            case 2: /* EXEC_NATIVE */
                execute_native_op(sheep_native_op(opcode));
                if(sheep_fn(opcode))
                    core.set_pc(get_lr());
                else
                    core.set_pc(op_pc + 4);
                return OpResult::Continue;
            default: /* EMUL_OP */
                execute_emul_op(sheep_emul_op(opcode) - 3);
                core.set_pc(op_pc + 4);
                return OpResult::Continue;
        }
    }

    void attention(cpu::Core &) override { handle_interrupt(); }
};

MacHost g_host;

/* 60Hz tick thread (cpu_ppc_kpx.cpp tick_thread_func). */
void tick_thread_func()
{
    int tick_counter = 0;
    uint64 start = GetTicks_usec();
    uint64 next = start;
    while(g_tick_running)
    {
        int period = 16625;
        next += period;
        int64 delay = next - GetTicks_usec();
        if(delay > 0)
        {
            struct timespec ts = { 0, (long)(delay * 1000) };
            nanosleep(&ts, nullptr);
        }
        else if(delay < -period)
            next = GetTicks_usec();
        if(tick_inhibit)
            continue;
        static const bool s_no_irq = []() {
            const char *e = std::getenv("MACEMU_PPC_NO_IRQ");
            return e && *e && *e != '0';
        }();
        if(s_no_irq)
            continue;

        if(++tick_counter > 60)
        {
            tick_counter = 0;
            WriteMacInt32(0x20c, (uint32)time(NULL) + 0x7C25B080);
            static const bool s_trace = []() {
                const char *e = std::getenv("MACEMU_MAMEPPC_TRACE");
                return e && *e && *e != '0';
            }();
            if(s_trace && g_core)
            {
                fprintf(stderr, "[mame-ppc] tick: pc=%08x lr=%08x r1=%08x r24=%08x r25=%08x msr=%08x mode=%u nest=%d\n",
                        g_core->pc(), get_lr(), gpr(1), gpr(24), gpr(25), g_core->reg(R::MSR),
                        ReadMacInt32(XLM_RUN_MODE), (int)ReadMacInt32(XLM_IRQ_NEST));
                uint32 sum = 0, nz = 0;
                for(uint32 i = 0; i < 640 * 480 * 4; i += 4)
                {
                    uint32 v = ReadMacInt32(screen_base + i);
                    sum += v;
                    nz += v != 0;
                }
                {
                    uint32 pc = g_core->pc();
                    char buf[96];
                    for(uint32 a = pc - 16; a <= pc + 12; a += 4)
                    {
                        g_core->disassemble(a, buf, sizeof buf);
                        fprintf(stderr, "[mame-ppc]   %c %08x: %08x  %s\n", a == pc ? '>' : ' ', a, ReadMacInt32(a), buf);
                    }
                }
                fprintf(stderr, "[mame-ppc] tick: fb@%08x sum=%08x nonzero=%u; native:", screen_base, sum, nz);
                for(int i = 0; i < 64; i++)
                    if(g_native_counts[i])
                        fprintf(stderr, " %d=%u", i, g_native_counts[i]);
                fprintf(stderr, "; m68k runs=%u handoffs=%u emulops=%u%s; emulop:", g_m68k_runs, g_m68k_handoffs,
                        g_m68k_emulops, g_m68k_active ? " ACTIVE" : "");
                if(g_m68k_active && g_m68k)
                {
                    char buf[96];
                    uint32 pc = g_m68k->pc();
                    g_m68k->disassemble(pc, buf, sizeof buf);
                    fprintf(stderr, " [68k pc=%08x sr=%04x a7=%08x: %s] vec28=%08x vec2c=%08x", pc,
                            g_m68k->reg(m68k::SR), g_m68k->reg(m68k::A7), buf, ReadMacInt32(0x28), ReadMacInt32(0x2c));
                }
                for(int i = 0; i < 64; i++)
                    if(g_emulop_counts[i])
                        fprintf(stderr, " %d=%u", i, g_emulop_counts[i]);
                fprintf(stderr, "\n");
            }
        }
        if(ReadMacInt32(XLM_IRQ_NEST) == 0)
        {
            SetInterruptFlag(INTFLAG_VIA);
            TriggerInterrupt();
        }
        if(g_platform.video_refresh)
            g_platform.video_refresh();
        boot_progress_export_cursor_to_ipc();
        boot_progress_export_app_to_ipc();
        boot_progress_export_mac_state();
    }
}

/* SIGSEGV: faults in Mac code are skipped (cpu_ppc_kpx.cpp). */
sigsegv_return_t mame_sigsegv_handler(sigsegv_info_t *sip)
{
    const uintptr addr = (uintptr)sigsegv_get_fault_address(sip);
    if((addr - (uintptr)ROMBaseHost) < ROM_SIZE)
        return SIGSEGV_RETURN_SKIP_INSTRUCTION;

    const uint32 pc = g_core ? g_core->pc() : 0;
    bool mac_fault = (pc >= ROMBase && pc < (ROMBase + ROM_AREA_SIZE))
                  || (pc >= RAMBase && pc < (RAMBase + RAMSize))
                  || (pc >= DR_CACHE_BASE && pc < (DR_CACHE_BASE + DR_CACHE_SIZE));
    if(mac_fault)
        return SIGSEGV_RETURN_SKIP_INSTRUCTION;

    fprintf(stderr, "SIGSEGV\n  pc %p\n  ea %p\n", (void *)(uintptr_t)pc, (void *)addr);
    return SIGSEGV_RETURN_FAILURE;
}

/* ---- Platform entries ---- */

bool mame_cpu_init(void)
{
    if(g_core)
        return true;
    g_memory.map(0, 1ull << 32, (uint8_t *)0);   /* identity, as Kheperix */
    cpu::Config config;
    config.arch = cpu::Arch::PPC;
    config.model = 750;
    config.mmu = false;      /* translation off: SheepShaver's machine */
    config.timers = false;   /* the tick thread paces the guest */
    config.exceptions = false; /* skip and log, as Kheperix */
    config.clock_hz = (uint32_t)CPUClockSpeed;
    config.jit = g_platform.ppc_jit;   /* --jit: the native back-end */
    g_core = cpu::create("mame-ppc", config, g_memory, g_host).release();
    if(!g_core)
    {
        fprintf(stderr, "[MAME-PPC] cannot create the mame-ppc core\n");
        return false;
    }
    g_core->reset();
    g_core->set_reg(R::MSR, 0);   /* vectors at 0, as the nanokernel expects */
    fprintf(stderr, "[MAME-PPC] %s core, PowerPC 750, %u MHz, %s\n", g_core->name(),
            (unsigned)(CPUClockSpeed / 1000000), config.jit ? "native back-end" : "UML interpreter");

    /* Initial state for the nanokernel boot (init_emul_ppc) */
    set_gpr(3, ROMBase + 0x30d000);
    set_gpr(4, KernelDataAddr + 0x1000);
    WriteMacInt32(XLM_RUN_MODE, MODE_68K);
    return true;
}

void mame_cpu_reset(void) {}
void mame_cpu_set_type(int, int) {}
int mame_cpu_execute_one(void) { return 1; }

void mame_cpu_execute_fast(void)
{
    if(!g_core)
        return;
    g_stop_requested = false;
    sigsegv_install_handler(mame_sigsegv_handler);

    /* The nanokernel probes these during init and must fault there. They
     * come back with OP_RESET's DR emulator setup. */
    munmap((void *)DR_EMULATOR_BASE, DR_EMULATOR_SIZE);
    munmap((void *)DR_CACHE_BASE, DR_CACHE_SIZE);

    uint32 entry = ROMBase + 0x310000;
    video_ppc_init_webrtc_null();
    tick_inhibit = true;

    g_tick_running = true;
    std::thread ticker(tick_thread_func);

    static const bool s_trace = []() {
        const char *e = std::getenv("MACEMU_MAMEPPC_TRACE");
        return e && *e && *e != '0';
    }();

    g_core->set_pc(entry);
    for(unsigned n = 0;; n++)
    {
        cpu::StopReason why = g_core->run();
        if(s_trace && (n < 50 || n % 1000 == 0))
            fprintf(stderr, "[mame-ppc] run %u: %s pc=%08x lr=%08x r1=%08x r3=%08x msr=%08x mode=%u\n", n,
                    why == cpu::StopReason::HostOp ? "hostop" : why == cpu::StopReason::Requested ? "requested" : "halted",
                    g_core->pc(), get_lr(), gpr(1), gpr(3), g_core->reg(R::MSR), ReadMacInt32(XLM_RUN_MODE));
        if(why == cpu::StopReason::Requested && g_stop_requested)
            break;
        if(g_exec_return)
            g_exec_return = false;   /* a stray EXEC_RETURN at top level */
    }

    g_tick_running = false;
    if(ticker.joinable())
        ticker.join();
}

void mame_cpu_request_stop(void)
{
    g_stop_requested = true;
    if(g_core)
        g_core->request_stop();
}

uint32_t mame_cpu_get_pc(void) { return g_core ? g_core->pc() : 0; }
uint16_t mame_cpu_get_sr(void) { return 0; }
uint32_t mame_cpu_get_dreg(int n) { return (g_core && n >= 0 && n < 32) ? gpr(n) : 0; }
uint32_t mame_cpu_get_areg(int) { return 0; }

void mame_cpu_trigger_interrupt(int)
{
    idle_resume();
    if(g_core)
        g_core->request_attention();
}

void mame_invoke_debug(void)
{
    idle_resume();
    ADBKeyDown(0x37);
    ADBKeyDown(0x7f);
    usleep(50000);
    ADBKeyUp(0x7f);
    ADBKeyUp(0x37);
}

void mame_cpu_execute_68k_trap(uint16_t trap, struct M68kRegisters *r)
{
    SheepVar proc_var(4);
    uint32 proc = proc_var.addr();
    WriteMacInt16(proc, trap);
    WriteMacInt16(proc + 2, M68K_RTS);
    execute_68k(proc, r);
}

void mame_cpu_execute_68k(uint32_t addr, struct M68kRegisters *r) { execute_68k(addr, r); }

void mame_flush_code_cache(void)
{
    if(g_core)
        g_core->invalidate(0, 0xFFFFFFFFu);
}

uint8_t mame_mem_read_byte(uint32_t addr) { return vm_read_memory_1(addr); }
uint16_t mame_mem_read_word(uint32_t addr) { return vm_read_memory_2(addr); }
uint32_t mame_mem_read_long(uint32_t addr) { return vm_read_memory_4(addr); }
void mame_mem_write_byte(uint32_t addr, uint8_t val) { vm_write_memory_1(addr, val); }
void mame_mem_write_word(uint32_t addr, uint16_t val) { vm_write_memory_2(addr, val); }
void mame_mem_write_long(uint32_t addr, uint32_t val) { vm_write_memory_4(addr, val); }
uint8_t *mame_mem_mac_to_host(uint32_t addr) { return vm_do_get_real_address(addr); }
uint32_t mame_mem_host_to_mac(uint8_t *ptr) { return vm_do_get_virtual_address(ptr); }

uint32_t mame_cpu_get_gpr(int n) { return (g_core && n >= 0 && n < 32) ? gpr(n) : 0; }
void mame_cpu_set_gpr(int n, uint32_t val)
{
    if(g_core && n >= 0 && n < 32)
        set_gpr(n, val);
}
uint32_t mame_cpu_get_cr(void) { return g_core ? get_cr() : 0; }
uint32_t mame_cpu_get_lr(void) { return g_core ? get_lr() : 0; }
uint32_t mame_cpu_get_ctr(void) { return g_core ? get_ctr() : 0; }
void mame_cpu_execute_ppc(uint32_t entry)
{
    if(g_core)
        run_until_exec_return(entry);
}

uint32_t mame_ppc_execute_macos_code(uint32_t tvect, int nargs, const uint32_t *args)
{
    return g_core ? execute_macos_code(tvect, nargs, args) : 0;
}

void mame_ppc_emulop_handler(void *r68k_regs, uint32_t pc, int selector)
{
    ppc::EmulOp(static_cast<M68kRegisters *>(r68k_regs), pc, selector);
}

void mame_ppc_native_op(uint32_t selector, uint32_t gprs[32])
{
    if(ppc_native_op_pure(selector, gprs))
        return;
    fprintf(stderr, "[MAME-PPC] ppc_native_op: selector %u needs execute_ppc\n", selector);
}

void mame_ppc_cursor_move(uint32_t mouse_base, int x, int y)
{
    static const uint8 proc_template[] = {
        0x2f, 0x08, 0x2f, 0x00, 0x2f, 0x01, 0x70, 0x01, 0xaa, 0xdb,
        M68K_RTS >> 8, M68K_RTS & 0xff
    };
    BUILD_SHEEPSHAVER_PROCEDURE(proc);
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.a[0] = ReadMacInt32(mouse_base + 4);
    r.d[0] = x;
    r.d[1] = y;
    execute_68k(proc, &r);
}

}  // namespace

extern "C" uint16_t kpx_make_emulop_shared(uint16_t op);

extern "C" void cpu_ppc_mame_install(Platform *p)
{
    p->cpu_name = "MAME-PPC";
    p->cpu_init = mame_cpu_init;
    p->cpu_reset = mame_cpu_reset;
    p->cpu_set_type = mame_cpu_set_type;
    p->cpu_execute_one = mame_cpu_execute_one;
    p->cpu_execute_fast = mame_cpu_execute_fast;
    p->cpu_request_stop = mame_cpu_request_stop;
    p->cpu_get_pc = mame_cpu_get_pc;
    p->cpu_get_sr = mame_cpu_get_sr;
    p->cpu_get_dreg = mame_cpu_get_dreg;
    p->cpu_get_areg = mame_cpu_get_areg;
    p->cpu_trigger_interrupt = mame_cpu_trigger_interrupt;
    p->invoke_debug = mame_invoke_debug;
    p->cpu_execute_68k_trap = mame_cpu_execute_68k_trap;
    p->cpu_execute_68k = mame_cpu_execute_68k;
    p->patch_after_startup = []() { ppc::PatchAfterStartup_PPC(); };
    p->flush_code_cache = mame_flush_code_cache;
    p->mem_read_byte = mame_mem_read_byte;
    p->mem_read_word = mame_mem_read_word;
    p->mem_read_long = mame_mem_read_long;
    p->mem_write_byte = mame_mem_write_byte;
    p->mem_write_word = mame_mem_write_word;
    p->mem_write_long = mame_mem_write_long;
    p->mem_mac_to_host = mame_mem_mac_to_host;
    p->mem_host_to_mac = mame_mem_host_to_mac;
    p->cpu_get_gpr = mame_cpu_get_gpr;
    p->cpu_set_gpr = mame_cpu_set_gpr;
    p->cpu_get_cr = mame_cpu_get_cr;
    p->cpu_get_lr = mame_cpu_get_lr;
    p->cpu_get_ctr = mame_cpu_get_ctr;
    p->cpu_execute_ppc = mame_cpu_execute_ppc;
    p->ppc_execute_macos_code = mame_ppc_execute_macos_code;
    p->make_emulop = kpx_make_emulop_shared;
    p->ppc_emulop_handler = mame_ppc_emulop_handler;
    p->ppc_native_op = mame_ppc_native_op;
    p->ppc_cursor_move = mame_ppc_cursor_move;
}

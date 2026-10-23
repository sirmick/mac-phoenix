/*
 * PowerCore.cpp - Executor's PowerPC on MAME's PowerPC recompiler.
 *
 * The core ("mame-ppc", a 750 with translation off) is made at the first
 * execute() and runs on syn68k_guest_memory(), so a guest address means the
 * same to both CPUs. Executor's processes run on host threads of their own
 * and may park inside a system call; the core keeps its stop flag per
 * thread, and a parked caller reloads the registers from PowerCoreState when
 * its system call returns (process switches save and restore that state with
 * the 68k context, syn68k_common.cpp).
 *
 * Exceptions are skipped and logged, as on the SheepShaver machine: the
 * vectors at 0 are Mac low memory here.
 */
#include "PowerCore.h"
#include <syn68k_public.h>
#include "cpu_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

namespace R = cpu::ppc;

/* Static: in the data window (3). */
uint32_t exit_cell;

std::atomic<cpu::Core *> the_core{nullptr};

void to_core(PowerCore &p, cpu::Core &c)
{
    for(int i = 0; i < 32; i++)
    {
        c.set_reg(R::GPR0 + i, p.r[i]);
        uint64_t bits;
        memcpy(&bits, &p.f[i], sizeof bits);
        c.set_fpr(i, bits);
    }
    c.set_reg(R::CR, p.cr);
    c.set_reg(R::LR, p.lr);
    c.set_reg(R::CTR, p.ctr);
    c.set_reg(R::XER, (c.reg(R::XER) & 0x1FFFFFFFu) | p.getXER());
    c.set_pc(p.CIA);
}

void from_core(PowerCore &p, cpu::Core &c)
{
    for(int i = 0; i < 32; i++)
    {
        p.r[i] = c.reg(R::GPR0 + i);
        uint64_t bits = c.fpr(i);
        memcpy(&p.f[i], &bits, sizeof bits);
    }
    p.cr = c.reg(R::CR);
    p.lr = c.reg(R::LR);
    p.ctr = c.reg(R::CTR);
    p.setXER(c.reg(R::XER));
    p.CIA = c.pc();
}

class PpcHost final : public cpu::Host
{
public:
    OpResult host_op(cpu::Core &c, uint32_t opcode, uint32_t op_pc) override
    {
        PowerCore &p = PowerCore::instance();
        if(opcode == POWERCORE_OP_EXIT && op_pc == p.exitAddress())
        {
            c.set_pc(op_pc);  /* stay on the exit cell */
            return OpResult::Stop;
        }
        if(opcode == POWERCORE_OP_SYSCALL && p.syscall)
        {
            from_core(p, c);
            p.CIA = op_pc;
            p.CIA = p.syscall(p);
            to_core(p, c);
            return OpResult::Continue;
        }
        fprintf(stderr, "executor: unexpected PowerPC host op %08x at %08x\n", opcode, op_pc);
        abort();
    }

    void attention(cpu::Core &c) override
    {
        PowerCore &p = PowerCore::instance();
        if(p.interruptFlag.test_and_set() || !p.handleInterrupt)
            return;
        from_core(p, c);
        p.handleInterrupt(p);
        to_core(p, c);
    }

    void slice_end(cpu::Core &) override
    {
        if(syn68k_slice_hook)
            syn68k_slice_hook();
    }
};
PpcHost ppc_host;

cpu::Core &core()
{
    if(cpu::Core *c = the_core.load())
        return *c;
    cpu::Config config;
    config.arch = cpu::Arch::PPC;
    config.model = 750;
    config.mmu = false;
    config.timers = false;      /* Executor's own timer thread */
    config.exceptions = false;  /* skip and log */
    config.jit = true;          /* the native back-end where there is one */
    cpu::Core *c = cpu::create("mame-ppc", config, syn68k_guest_memory(), ppc_host).release();
    if(!c)
    {
        fprintf(stderr, "executor: cannot start the mame-ppc core\n");
        abort();
    }
    c->reset();
    c->set_reg(R::MSR, 0x2000);  /* supervisor, FP available, big-endian */
    the_core = c;
    return *c;
}

/* Executor's debugger (never attached in MacPhoenix). mame-ppc's step()
 * runs a whole recompiled block, so a breakpoint is only seen where a
 * block starts. */
void step_with_debugger(PowerCore &p, cpu::Core &c)
{
    uint32_t pc = c.pc();
    if(p.getNextBreakpoint(pc) == pc)
    {
        from_core(p, c);
        p.debugger(p);
        to_core(p, c);
        if(p.CIA != pc)
            return;
    }
    c.step();
}

}  // namespace

PowerCore &PowerCore::instance()
{
    static PowerCore p;
    return p;
}

uint32_t PowerCore::exitAddress()
{
    uint32_t addr = US_TO_SYN68K(&exit_cell);
    if(!exit_cell)
        syn68k_guest_memory().write32(addr, POWERCORE_OP_EXIT);
    return addr;
}

void PowerCore::requestInterrupt()
{
    interruptFlag.clear();
    if(cpu::Core *c = the_core.load())
        c->request_attention();
}

void PowerCore::execute()
{
    cpu::Core &c = core();
    uint32_t exit = exitAddress();
    to_core(*this, c);
    c.request_attention();  /* an interrupt asked for before we ran */
    bool debugging = debugger && getNextBreakpoint;
    while(c.pc() != exit)
    {
        if(debugging)
            step_with_debugger(*this, c);
        else
            c.run();
    }
    from_core(*this, c);
}

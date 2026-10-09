/*
 * cpu_m68k.cpp - the ROM-based 68k Mac on a cpu::Core.
 *
 * The Platform's memory, register, Execute68k and interrupt entries come
 * from platform_cpu (as on every machine). This file adds the machine:
 *
 *   cpu_init          create the core from CPUType / FPUType /
 *                     TwentyFourBitAddressing (set from the ROM's machine
 *                     profile by CPUContext)
 *   cpu_reset         power-on, then PC at ROM+$2A, SSP $2000, as UAE's
 *                     m68k_reset has always done (no reset vectors)
 *   cpu_execute_fast  run until request_stop
 *   cpu_execute_one   one instruction
 *   invoke_debug      NMI
 *
 * RomHost answers the core: $7100 returns to the host (Execute68k), every
 * other $71xx is an EmulOp; the interrupt line follows InterruptFlags
 * (level 1 while any is set; INTFLAG_NMI is a level-7 edge, consumed when
 * delivered).
 */
#include "cpu_m68k.h"
#include "platform_cpu.h"
#include "m68k_registers.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "sysdeps.h"
#include "main.h"

namespace m68k {
void EmulOp(uint16_t opcode, struct M68kRegisters *r);
}
extern uint32_t ROMBaseMac;
extern int CPUType, FPUType;
extern bool TwentyFourBitAddressing;

namespace {

cpu::GuestMemory memory;
cpu::Core *core;  /* lives until the process exits */
std::string core_name = "uae";
bool use_jit = false;

int base_level()
{
    return (InterruptFlags & ~(uint32_t)INTFLAG_NMI) ? 1 : 0;
}

class RomHost final : public cpu::Host
{
public:
    OpResult host_op(cpu::Core &c, uint32_t opcode, uint32_t op_pc) override
    {
        if(opcode == cpu::m68k::HOST_OP_RETURN)
        {
            c.set_pc(op_pc);
            return OpResult::Stop;
        }
        M68kRegisters r;
        for(int i = 0; i < 8; i++)
        {
            r.d[i] = c.reg(cpu::m68k::D0 + i);
            r.a[i] = c.reg(cpu::m68k::A0 + i);
        }
        r.sr = (uint16_t)c.reg(cpu::m68k::SR);
        m68k::EmulOp((uint16_t)opcode, &r);
        for(int i = 0; i < 8; i++)
        {
            c.set_reg(cpu::m68k::D0 + i, r.d[i]);
            c.set_reg(cpu::m68k::A0 + i, r.a[i]);
        }
        /* Always written back: on UAE this re-checks for interrupts, as
         * its built-in EmulOp path did. */
        c.set_reg(cpu::m68k::SR, r.sr);
        /* The IRQ EmulOp drains InterruptFlags. */
        c.set_irq(base_level());
        return OpResult::Continue;
    }

    void attention(cpu::Core &c) override
    {
        if(InterruptFlags & INTFLAG_NMI)
        {
            ClearInterruptFlag(INTFLAG_NMI);
            c.set_irq(7);
        }
        c.set_irq(base_level());
    }
};
RomHost rom_host;

bool m68k_cpu_init(void)
{
    if(core)
        return true;
    cpu::Config config;
    config.arch = cpu::Arch::M68K;
    config.model = 68000 + 10 * CPUType;  /* 4 -> 68040 */
    config.fpu = FPUType != 0;
    config.address_bits = TwentyFourBitAddressing ? 24 : 32;
    config.jit = use_jit;
    memory.set_address_bits(config.address_bits);
    core = cpu::create(core_name, config, memory, rom_host).release();
    if(!core)
    {
        fprintf(stderr, "[CPU] cannot start the %s core as a %d (%d-bit%s)\n", core_name.c_str(),
                config.model, config.address_bits, config.fpu ? ", FPU" : "");
        return false;
    }
    cpu::platform_set_core(core);
    fprintf(stderr, "[CPU] %s core, %d%s, %d-bit\n", core->name(), config.model,
            config.fpu ? " + FPU" : "", config.address_bits);
    return true;
}

void m68k_cpu_reset(void)
{
    core->reset();
    core->set_reg(cpu::m68k::SR, 0x2700);
    core->set_reg(cpu::m68k::A7, 0x2000);
    core->set_pc(ROMBaseMac + 0x2a);
}

void m68k_cpu_set_type(int cpu_type, int fpu_type)
{
    CPUType = cpu_type;
    FPUType = fpu_type;
}

void m68k_cpu_execute_fast(void)
{
    while(core->run() != cpu::StopReason::Requested)
        ;
}

int m68k_cpu_execute_one(void)
{
    core->step();
    return 0;
}

void m68k_invoke_debug(void)
{
    SetInterruptFlag(INTFLAG_NMI);
    if(core)
        core->request_attention();
}

}  // namespace

cpu::GuestMemory &cpu_m68k_memory()
{
    return memory;
}

void cpu_m68k_install(Platform *p, const char *name, bool jit)
{
    if(name && *name)
        core_name = name;
    use_jit = jit;
    cpu::platform_install(p);
    cpu::platform_set_memory(&memory);
    p->cpu_name = "68k";
    p->cpu_init = m68k_cpu_init;
    p->cpu_reset = m68k_cpu_reset;
    p->cpu_set_type = m68k_cpu_set_type;
    p->cpu_execute_fast = m68k_cpu_execute_fast;
    p->cpu_execute_one = m68k_cpu_execute_one;
    p->invoke_debug = m68k_invoke_debug;
}

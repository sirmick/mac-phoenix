/*
 * platform_cpu.cpp - g_platform's CPU and memory entries on a cpu::Core.
 */
#include "platform_cpu.h"
#include "m68k_registers.h"

#include <cstdio>
#include <cstdlib>

namespace cpu {
namespace {

GuestMemory *mem;
Core *core;

/* Host pointers for unmapped guest addresses land here, as UAE's dummy
 * page did: callers that write through them must not crash. */
alignas(16) uint8_t dummy_page[4096];

Core &need_core()
{
    if(!core)
    {
        fprintf(stderr, "platform: no CPU core yet\n");
        abort();
    }
    return *core;
}

uint8_t mem_rb(uint32_t a) { return mem->read8(a); }
uint16_t mem_rw(uint32_t a) { return mem->read16(a); }
uint32_t mem_rl(uint32_t a) { return mem->read32(a); }
void mem_wb(uint32_t a, uint8_t v) { mem->write8(a, v); }
void mem_ww(uint32_t a, uint16_t v) { mem->write16(a, v); }
void mem_wl(uint32_t a, uint32_t v) { mem->write32(a, v); }

uint8_t *mem_m2h(uint32_t a)
{
    uint8_t *p = mem->host(a);
    return p ? p : dummy_page + (a & (sizeof dummy_page - 1));
}

uint32_t mem_h2m(uint8_t *p)
{
    uint32_t a = 0;
    if(!mem->guest(p, &a))
        fprintf(stderr, "platform: host pointer %p is not guest memory\n", (void *)p);
    return a;
}

uint32_t get_pc(void) { return core ? core->pc() : 0; }
uint16_t get_sr(void) { return core ? (uint16_t)core->reg(m68k::SR) : 0; }
uint32_t get_dreg(int n) { return core ? core->reg(m68k::D0 + n) : 0; }
uint32_t get_areg(int n) { return core ? core->reg(m68k::A0 + n) : 0; }

void request_stop(void)
{
    if(core)
        core->request_stop();
}

void trigger_interrupt(int)
{
    if(core)
        core->request_attention();
}

void flush_code_cache(void)
{
    if(core)
        core->invalidate(0, 0xFFFFFFFFu);
}

void load_regs(Core &c, const M68kRegisters *r)
{
    for(int i = 0; i < 8; i++)
        c.set_reg(m68k::D0 + i, r->d[i]);
    for(int i = 0; i < 7; i++)
        c.set_reg(m68k::A0 + i, r->a[i]);
}

void store_regs(Core &c, M68kRegisters *r)
{
    for(int i = 0; i < 8; i++)
        r->d[i] = c.reg(m68k::D0 + i);
    for(int i = 0; i < 7; i++)
        r->a[i] = c.reg(m68k::A0 + i);
}

/* Basilisk II's Execute68k: d0-d7/a0-a6 in and out; a7 and sr are not
 * part of the call. The return address points at a "return to host" op
 * pushed just above it. */
void execute_68k(uint32_t addr, M68kRegisters *r)
{
    Core &c = need_core();
    uint32_t saved_pc = c.pc();
    load_regs(c, r);
    uint32_t sp = c.reg(m68k::A7) - 2;
    mem->write16(sp, m68k::HOST_OP_RETURN);
    uint32_t ret = sp;
    sp -= 4;
    mem->write32(sp, ret);
    c.set_reg(m68k::A7, sp);
    c.run_until(addr, ret);
    c.set_reg(m68k::A7, c.reg(m68k::A7) + 2);  /* RTS left it on the op */
    store_regs(c, r);
    c.set_pc(saved_pc);
}

/* Basilisk II's Execute68kTrap: [trap][return to host] on the stack, run
 * from the trap. */
void execute_68k_trap(uint16_t trap, M68kRegisters *r)
{
    Core &c = need_core();
    uint32_t saved_pc = c.pc();
    load_regs(c, r);
    uint32_t sp = c.reg(m68k::A7) - 2;
    mem->write16(sp, m68k::HOST_OP_RETURN);
    sp -= 2;
    mem->write16(sp, trap);
    c.set_reg(m68k::A7, sp);
    c.run_until(sp, sp + 2);
    c.set_reg(m68k::A7, c.reg(m68k::A7) + 4);
    store_regs(c, r);
    c.set_pc(saved_pc);
}

}  // namespace

void platform_install(Platform *p)
{
    p->mem_read_byte = mem_rb;
    p->mem_read_word = mem_rw;
    p->mem_read_long = mem_rl;
    p->mem_write_byte = mem_wb;
    p->mem_write_word = mem_ww;
    p->mem_write_long = mem_wl;
    p->mem_mac_to_host = mem_m2h;
    p->mem_host_to_mac = mem_h2m;
    p->cpu_get_pc = get_pc;
    p->cpu_get_sr = get_sr;
    p->cpu_get_dreg = get_dreg;
    p->cpu_get_areg = get_areg;
    p->cpu_request_stop = request_stop;
    p->cpu_trigger_interrupt = trigger_interrupt;
    p->cpu_execute_68k = execute_68k;
    p->cpu_execute_68k_trap = execute_68k_trap;
    p->flush_code_cache = flush_code_cache;
}

void platform_set_memory(GuestMemory *memory) { mem = memory; }
void platform_set_core(Core *c) { core = c; }
GuestMemory *platform_memory() { return mem; }
Core *platform_core() { return core; }

}  // namespace cpu

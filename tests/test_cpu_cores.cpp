/*
 * test_cpu_cores.cpp - two mame-68k cores in one process (docs/cpu/PLAN.md,
 * C1c). Each gets its own 64KB of guest RAM and a different program; both
 * run to a host op, and neither disturbs the other. Then a context round
 * trip on one of them.
 *
 * Host-only: no ROM, no emulator. Links the MAME 68k library and
 * GuestMemory directly (the UAE and Musashi cores need the Platform
 * table, so cpu_core as a whole is not linked here).
 */
#include "cpu_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace cpu {
std::unique_ptr<Core> create_mame_68k(const Config &, GuestMemory &, Host &);
std::unique_ptr<Core> create_mame_ppc(const Config &, GuestMemory &, Host &);
}

static int failures;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        if(!(cond))                                                                  \
        {                                                                            \
            failures++;                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                            \
    } while(0)

namespace {

/* Stops on the architecture's "return" host op ($7100; PPC selector 0),
 * counts every other host op. */
struct TestHost final : cpu::Host
{
    int other_ops = 0;
    uint32_t last_op_pc = 0;
    OpResult host_op(cpu::Core &core, uint32_t opcode, uint32_t op_pc) override
    {
        last_op_pc = op_pc;
        bool ret = core.arch() == cpu::Arch::M68K ? opcode == cpu::m68k::HOST_OP_RETURN
                                                   : (opcode & 0x03ffffff) == 0;
        if(ret)
        {
            core.set_pc(op_pc); /* leave the PC on the op, as the contract says */
            return OpResult::Stop;
        }
        other_ops++;
        return OpResult::Continue;
    }
};

/* A PowerPC machine: 64KB at 0, a program at 0x1000. */
struct PpcMachine
{
    std::vector<uint8_t> ram;
    cpu::GuestMemory mem;
    TestHost host;
    std::unique_ptr<cpu::Core> core;

    PpcMachine(const uint32_t *program, size_t words, bool jit) : ram(cpu::GuestMemory::PAGE_SIZE)
    {
        mem.map(0, ram.size(), ram.data());
        for(size_t i = 0; i < words; i++)
            mem.write32(0x1000 + 4 * i, program[i]);
        cpu::Config config;
        config.arch = cpu::Arch::PPC;
        config.model = 750;
        config.jit = jit;
        core = cpu::create_mame_ppc(config, mem, host);
    }
};

/* jit: the native back-end (where there is one) or the UML interpreter. */
static int test_ppc(bool jit)
{
    /* addi r3,r0,5 ; addi r3,r3,1 ; stw r3,0x2000(r0) ; host op 0x18000003 ;
     * addi r4,r0,7 ; host op 0 (return) */
    static const uint32_t prog[] = { 0x38600005, 0x38630001, 0x90602000, 0x18000003, 0x38800007, 0x18000000 };
    PpcMachine p(prog, 6, jit);
    CHECK(p.core != nullptr);
    if(!p.core)
        return 1;
    p.core->reset();
    p.core->set_pc(0x1000);
    CHECK(p.core->pc() == 0x1000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->pc() == 0x1014);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 6);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 4) == 7);
    CHECK(p.mem.read32(0x2000) == 6);
    CHECK(p.host.other_ops == 1);

    /* Run again from the top: the compiled block is reused. */
    p.core->set_pc(0x1000);
    p.core->set_reg(cpu::ppc::GPR0 + 3, 0);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 6);
    CHECK(p.host.other_ops == 2);

    /* Change the code under the recompiler: addi r3,r0,9. */
    p.mem.write32(0x1000, 0x38600009);
    p.core->invalidate(0x1000, 4);
    p.core->set_pc(0x1000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 10);

    /* Context round trip and the disassembler. */
    std::vector<uint8_t> ctx(p.core->context_size());
    p.core->save_context(ctx.data());
    p.core->set_reg(cpu::ppc::GPR0 + 3, 99);
    p.core->restore_context(ctx.data());
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 10);
    char buf[64];
    int n = p.core->disassemble(0x1004, buf, sizeof buf);
    CHECK(n == 4);
    CHECK(strstr(buf, "addi") != nullptr);
    return 0;
}

struct Machine
{
    std::vector<uint8_t> ram;
    cpu::GuestMemory mem;
    TestHost host;
    std::unique_ptr<cpu::Core> core;

    explicit Machine(const uint16_t *program, size_t words)
        : ram(cpu::GuestMemory::PAGE_SIZE)
    {
        mem.map(0, ram.size(), ram.data());
        mem.write32(0, 0x8000);  /* reset SSP */
        mem.write32(4, 0x1000);  /* reset PC */
        for(size_t i = 0; i < words; i++)
            mem.write16(0x1000 + 2 * i, program[i]);
        cpu::Config config;
        config.arch = cpu::Arch::M68K;
        config.model = 68040;
        config.fpu = true;
        core = cpu::create_mame_68k(config, mem, host);
    }
};

}  // namespace

int main()
{
    /* A: d0 = 5 + 1; B: d0 = -1, d1 = 0x1234 via memory. */
    static const uint16_t prog_a[] = { 0x7005, 0x5280, 0x7100 };
    static const uint16_t prog_b[] = { 0x70FF, 0x323C, 0x1234, 0x31C1, 0x2000, 0x7100 };

    Machine a(prog_a, 3), b(prog_b, 6);
    CHECK(a.core != nullptr);
    CHECK(b.core != nullptr);
    if(!a.core || !b.core)
        return 1;

    a.core->reset();
    b.core->reset();
    CHECK(a.core->pc() == 0x1000);
    CHECK(a.core->reg(cpu::m68k::A7) == 0x8000);

    /* Run B first, then A: A's state must not have been touched. */
    CHECK(b.core->run() == cpu::StopReason::HostOp);
    CHECK(b.core->pc() == 0x100A);
    CHECK(b.core->reg(cpu::m68k::D0) == 0xFFFFFFFFu);
    CHECK(b.core->reg(cpu::m68k::D1) == 0x1234);
    CHECK(b.mem.read16(0x2000) == 0x1234);
    CHECK(a.mem.read16(0x2000) == 0);
    CHECK(a.core->pc() == 0x1000);

    CHECK(a.core->run() == cpu::StopReason::HostOp);
    CHECK(a.core->pc() == 0x1004);
    CHECK(a.core->reg(cpu::m68k::D0) == 6);
    CHECK(b.core->reg(cpu::m68k::D0) == 0xFFFFFFFFu);

    /* Context round trip on A: change registers, restore, check. */
    std::vector<uint8_t> ctx(a.core->context_size());
    a.core->save_context(ctx.data());
    a.core->set_reg(cpu::m68k::D0, 99);
    a.core->set_reg(cpu::m68k::A3, 0x4000);
    a.core->set_pc(0x1000);
    CHECK(a.core->reg(cpu::m68k::D0) == 99);
    a.core->restore_context(ctx.data());
    CHECK(a.core->reg(cpu::m68k::D0) == 6);
    CHECK(a.core->reg(cpu::m68k::A3) == 0);
    CHECK(a.core->pc() == 0x1004);

    /* Registers through the interface: SR and the stack pointers. */
    CHECK((a.core->reg(cpu::m68k::SR) & 0x2000) != 0); /* supervisor after reset */
    a.core->set_reg(cpu::m68k::USP, 0x7000);
    CHECK(a.core->reg(cpu::m68k::USP) == 0x7000);
    CHECK(a.core->reg(cpu::m68k::A7) == 0x8000); /* ISP is the active one */

    /* Disassembler. */
    char buf[64];
    int n = a.core->disassemble(0x1000, buf, sizeof buf);
    CHECK(n == 2);
    CHECK(strstr(buf, "moveq") != nullptr);

    CHECK(a.host.other_ops == 0 && b.host.other_ops == 0);

    test_ppc(false);
    test_ppc(true);

    if(failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_cpu_cores: ok\n");
    return 0;
}

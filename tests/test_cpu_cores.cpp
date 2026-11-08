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
#include <chrono>
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

    PpcMachine(const uint32_t *program, size_t words, bool jit, bool fast = false, bool exceptions = true,
               bool hooks = false)
        : ram(cpu::GuestMemory::PAGE_BYTES)
    {
        mem.map(0, ram.size(), ram.data());
        for(size_t i = 0; i < words; i++)
            mem.write32(0x1000 + 4 * i, program[i]);
        cpu::Config config;
        config.arch = cpu::Arch::PPC;
        config.model = 750;
        config.jit = jit;
        config.exceptions = exceptions;
        config.instruction_hook = hooks;
        if(fast)
            config.fast_memory = { { 0, (uint32_t)ram.size(), false } };
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

/* Code changed in a sequence reached by a local branch from an unchanged
 * one of the same block (the block's checksum is at each sequence's head,
 * and the branch goes past it): invalidate() of just that range makes the
 * new code run (CFM loading a fragment where another's code was).
 * cmpwi r4,0 ; beq +8 ; addi r3,r0,2 ; addi r3,r0,1 ; host op 0 */
static void test_ppc_invalidate_branch_target(bool jit)
{
    static const uint32_t prog[] = { 0x2c040000, 0x41820008, 0x38600002, 0x38600001, 0x18000000 };
    PpcMachine p(prog, 5, jit);
    p.core->reset();
    p.core->set_pc(0x1000);
    p.core->set_reg(cpu::ppc::GPR0 + 4, 0);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 1);

    p.mem.write32(0x100c, 0x38600005);   /* addi r3,r0,5 */
    p.core->invalidate(0x100c, 4);
    p.core->set_pc(0x1000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 5);
}

/* The same through the guest's own icbi (Mac OS's MakeDataExecutable on
 * the ROM machine), for a block entered on the page after the rewritten
 * one that branches back into it: its hash entry is outside the page, so
 * dropping the page's entries alone left the old code running (QuickTime
 * 3's PowerPC codecs on 8.6).
 *   0x1ff8 L: addi r3,r0,1 ; b X
 *   0x2000 E: cmpwi r4,0 ; beq L
 *   0x2008 X: host op 0
 *   0x3000:   stw r5,0(r6) ; dcbst 0,r6 ; sync ; icbi 0,r6 ; isync ; host op 0 */
static void test_ppc_icbi_backward_branch(bool jit)
{
    static const uint32_t nop[] = { 0x60000000 };
    PpcMachine p(nop, 1, jit);
    static const uint32_t code[] = { 0x38600001, 0x4800000c, 0x2c040000, 0x4182fff4, 0x18000000 };
    for(size_t i = 0; i < 5; i++)
        p.mem.write32(0x1ff8 + 4 * i, code[i]);
    static const uint32_t flush[] = { 0x90a60000, 0x7c00306c, 0x7c0004ac, 0x7c0037ac, 0x4c00012c, 0x18000000 };
    for(size_t i = 0; i < 6; i++)
        p.mem.write32(0x3000 + 4 * i, flush[i]);
    p.core->reset();
    p.core->set_reg(cpu::ppc::GPR0 + 4, 0);
    p.core->set_pc(0x2000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 1);

    p.core->set_reg(cpu::ppc::GPR0 + 5, 0x38600005);   /* addi r3,r0,5 */
    p.core->set_reg(cpu::ppc::GPR0 + 6, 0x1ff8);
    p.core->set_pc(0x3000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.mem.read32(0x1ff8) == 0x38600005);

    p.core->set_reg(cpu::ppc::GPR0 + 3, 0);
    p.core->set_pc(0x2000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 5);
}

/* A double stored to and loaded from an address that isn't word-aligned:
 * the 750 takes an alignment exception, which the installer completes in
 * software (as Mac OS's handler does; BBEdit Lite 4.1's setjmp).
 * lfd f1,0x3000(0) ; addi r3,r0,0x2001 ; stfd f1,0(r3) ; lfd f2,0(r3) ;
 * stfdu f2,4(r3) ; stfd f2,0x3010(0) ; host op 0 */
static void test_ppc_misaligned_fp(bool jit)
{
    static const uint32_t prog[] = { 0xC8203000, 0x38602001, 0xD8230000, 0xC8430000,
                                     0xDC430004, 0xD8403010, 0x18000000 };
    PpcMachine p(prog, 7, jit, false, false);   /* as Executor and the kpx machine */
    if(!p.core)
        return;
    p.core->reset();
    p.mem.write32(0x3000, 0x400921FB);   /* pi */
    p.mem.write32(0x3004, 0x54442D18);
    p.core->set_pc(0x1000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(p.mem.read8(0x2001) == 0x40 && p.mem.read8(0x2004) == 0xFB && p.mem.read8(0x2008) == 0xFB);
    CHECK(p.mem.read32(0x3010) == 0x400921FB && p.mem.read32(0x3014) == 0x54442D18);
    CHECK(p.mem.read8(0x2005) == 0x40 && p.mem.read8(0x200C) == 0x18);   /* stfdu at 0x2005 */
    CHECK(p.core->reg(cpu::ppc::GPR0 + 3) == 0x2005);

    /* ... and stmw/lmw: addi r30,r0,0x1234 ; addi r31,r0,0x5678 ;
     * addi r3,r0,0x2402 ; stmw r30,0(r3) ; addi r30,r0,0 ; addi r31,r0,0 ;
     * lmw r30,0(r3) ; host op 0 */
    static const uint32_t prog2[] = { 0x3BC01234, 0x3BE05678, 0x38602402, 0xBFC30000,
                                      0x3BC00000, 0x3BE00000, 0xBBC30000, 0x18000000 };
    PpcMachine q(prog2, 8, jit, false, false);
    q.core->reset();
    q.core->set_pc(0x1000);
    CHECK(q.core->run() == cpu::StopReason::HostOp);
    CHECK(q.mem.read8(0x2404) == 0x12 && q.mem.read8(0x2405) == 0x34 && q.mem.read8(0x2409) == 0x78);
    CHECK(q.core->reg(cpu::ppc::GPR0 + 30) == 0x1234 && q.core->reg(cpu::ppc::GPR0 + 31) == 0x5678);
}

/* Config::instruction_hook: the hook sees every instruction, in order, the
 * registers as the instructions before it left them; across a branch and
 * a host op; then none once it is cleared. A core made without it refuses. */
static void test_ppc_instruction_hook(bool jit)
{
    /* addi r3,r0,5 ; b +8 ; (addi r3,r0,99) ; addi r3,r3,1 ; host op 3 ;
     * addi r4,r0,7 ; host op 0 */
    static const uint32_t prog[] = { 0x38600005, 0x48000008, 0x38600063, 0x38630001,
                                     0x18000003, 0x38800007, 0x18000000 };
    PpcMachine plain(prog, 7, jit);
    CHECK(!plain.core->set_instruction_hook([](cpu::Core &, uint32_t, void *) {}, nullptr));

    struct Seen { uint32_t pc, r3; };
    std::vector<Seen> seen;
    PpcMachine p(prog, 7, jit, false, true, true);
    p.core->reset();
    CHECK(p.core->set_instruction_hook(
        [](cpu::Core &c, uint32_t pc, void *ctx) {
            CHECK(c.pc() == pc);
            ((std::vector<Seen> *)ctx)->push_back({ pc, c.reg(cpu::ppc::GPR0 + 3) });
        },
        &seen));
    p.core->set_pc(0x1000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    static const Seen want[] = { { 0x1000, 0 }, { 0x1004, 5 }, { 0x100C, 5 }, { 0x1010, 6 },
                                 { 0x1014, 6 }, { 0x1018, 6 } };
    CHECK(seen.size() == std::size(want));
    for(size_t i = 0; i < seen.size() && i < std::size(want); i++)
        CHECK(seen[i].pc == want[i].pc && seen[i].r3 == want[i].r3);
    CHECK(p.core->reg(cpu::ppc::GPR0 + 4) == 7);

    seen.clear();
    CHECK(p.core->set_instruction_hook(nullptr, nullptr));
    p.core->set_pc(0x1000);
    CHECK(p.core->run() == cpu::StopReason::HostOp);
    CHECK(seen.empty() && p.core->reg(cpu::ppc::GPR0 + 3) == 6);
}

/* --bench [N]: N million (default 1) iterations of lwz/addi/stw/addic./bne
 * (5N million instructions) on each back-end, with and without inline
 * memory. Prints millions of guest instructions a second. */
static void bench_ppc(bool jit, bool fast, unsigned millions)
{
    uint32_t prog[] = {
        0x3CA00000 | (millions << 4),  /* addis r5,r0,N*0x10   (N*1M iterations) */
        0x38802000,  /* addi r4,r0,0x2000 */
        0x80640000,  /* loop: lwz r3,0(r4) */
        0x38630001,  /* addi r3,r3,1 */
        0x90640000,  /* stw r3,0(r4) */
        0x34A5FFFF,  /* addic. r5,r5,-1 */
        0x4082FFF0,  /* bne loop */
        0x18000000,  /* host op: return */
    };
    PpcMachine p(prog, sizeof prog / 4, jit, fast);
    p.core->reset();
    p.core->set_pc(0x1000);
    auto t0 = std::chrono::steady_clock::now();
    p.core->run();
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("bench ppc %-11s %-6s: %u iterations, %.3f s, %.1f MIPS\n", jit ? "native" : "uml-interp",
           fast ? "inline" : "calls", p.mem.read32(0x2000), s, 5.0 * millions / s);
}

struct Machine
{
    std::vector<uint8_t> ram;
    cpu::GuestMemory mem;
    TestHost host;
    std::unique_ptr<cpu::Core> core;

    explicit Machine(const uint16_t *program, size_t words)
        : ram(cpu::GuestMemory::PAGE_BYTES)
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

int main(int argc, char **argv)
{
    if(argc > 1 && strcmp(argv[1], "--bench") == 0)
    {
        unsigned millions = argc > 2 ? (unsigned)atoi(argv[2]) : 1;
        if(millions < 1 || millions > 0xfff)
            millions = 1;
        for(int jit = 0; jit < 2; jit++)
            for(int fast = 0; fast < 2; fast++)
                bench_ppc(jit, fast, millions);
        return 0;
    }
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
    test_ppc_invalidate_branch_target(false);
    test_ppc_invalidate_branch_target(true);
    test_ppc_icbi_backward_branch(false);
    test_ppc_icbi_backward_branch(true);
    test_ppc_misaligned_fp(false);
    test_ppc_misaligned_fp(true);
    test_ppc_instruction_hook(false);
    test_ppc_instruction_hook(true);

    if(failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("test_cpu_cores: ok\n");
    return 0;
}

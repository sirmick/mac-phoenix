/*
 * uae_core.cpp - cpu::Core on MacPhoenix's UAE interpreter (src/cpu/uae_cpu).
 *
 * UAE reaches its driver through uae_host_hooks (uae_cpu/uae_host_hooks.h),
 * which a UaeCore fills in:
 *
 *   emulop   host ops $7101-$71FF
 *   intlev   the interrupt line; serves request_attention() first
 *   poll     UAE's tick check: serves request_attention()
 *
 * $7100 is UAE's built-in EMULOP_RETURN: it leaves m68k_execute() with the
 * PC still on it. run() then hands it to the Host like any other host op.
 *
 * Memory: UAE reads and writes data through g_platform.mem_*, which must
 * be this core's GuestMemory (platform_cpu.h). It fetches instructions
 * through one host base pointer (MEMBaseDiff), so the GuestMemory must be
 * one window or several with the same base offset (GuestMemory::uniform);
 * create_uae() returns null otherwise.
 *
 * JIT (Config::jit): the outermost run() runs compiled code; nested runs
 * (host code calling guest code) interpret, as Basilisk II does.
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
#include "uae_host_hooks.h"
#include "compiler/compemu.h"
#include "vm_alloc.h"

#include "cpu_core.h"
#include "platform_cpu.h"

#include <atomic>
#include <cstdio>

extern bool quit_program;
extern uintptr MEMBaseDiff;
extern int CPUType, FPUType;
extern bool UseJIT;

namespace cpu {
namespace {

class UaeCore;
UaeCore *g_core;
GuestMemory *g_mem;

void emulop_hook(uint16_t opcode);
int intlev_hook(void);
void poll_hook(void);

class UaeCore final : public Core
{
public:
    UaeCore(const Config &config, intptr_t delta, GuestMemory &memory, Host &host)
        : host_(host)
    {
        g_core = this;
        g_mem = &memory;
        CPUType = config.model / 10 % 10;  /* 68040 -> 4 */
        FPUType = config.fpu ? 1 : 0;
        MEMBaseDiff = (uintptr)delta;
        /* A 24-bit CPU ignores the top address byte (Memory Manager
         * flags); UAE masks in do_get_real_address. */
        address_reg_mask = config.address_bits == 24 ? 0x00FFFFFFu : 0xFFFFFFFFu;

        uae_host_hooks.emulop = emulop_hook;
        uae_host_hooks.intlev = intlev_hook;
        uae_host_hooks.poll = poll_hook;

        init_m68k();
#if USE_JIT
        UseJIT = false;
        if(config.jit)
        {
            vm_init();
            UseJIT = compiler_use_jit();
            if(UseJIT)
                compiler_init();
        }
#endif
    }

    ~UaeCore() override
    {
        uae_host_hooks.emulop = nullptr;
        uae_host_hooks.intlev = nullptr;
        uae_host_hooks.poll = nullptr;
        g_core = nullptr;
        g_mem = nullptr;
    }

    const char *name() const override { return "uae"; }
    Arch arch() const override { return Arch::M68K; }

    void reset() override { m68k_reset(); }

    StopReason run() override
    {
        bool stopped = false;
        bool *outer = stop_flag_;
        stop_flag_ = &stopped;
        StopReason why;
        for(;;)
        {
            serve_attention();
            quit_program = false;
            ++depth_;
#if USE_JIT
            if(UseJIT && depth_ == 1)
                m68k_compile_execute();
            else
#endif
                m68k_execute();
            --depth_;
            quit_program = false;
            if(stopped)
            {
                why = StopReason::HostOp;
                break;
            }
            if(stop_requested_.exchange(false))
            {
                why = StopReason::Requested;
                break;
            }
            /* EMULOP_RETURN left the PC on the $7100. (Otherwise UAE took
             * an interrupt in the pass that honoured the exit: run on.) */
            uaecptr pc = m68k_getpc();
            if(get_word(pc) == m68k::HOST_OP_MATCH && exit_op(pc))
            {
                why = StopReason::HostOp;
                break;
            }
        }
        stop_flag_ = outer;
        return why;
    }

    void step() override
    {
        bool stopped = false;
        bool *outer = stop_flag_;
        stop_flag_ = &stopped;
        uaecptr pc = m68k_getpc();
        if(get_word(pc) == m68k::HOST_OP_MATCH)
            exit_op(pc);
        else
        {
            uae_u32 opcode = GET_OPCODE;
            (*cpufunctbl[opcode])(opcode);
            cpu_check_ticks();
            if(SPCFLAGS_TEST(SPCFLAG_ALL_BUT_EXEC_RETURN))
                m68k_do_specialties();
        }
        quit_program = false;
        stop_flag_ = outer;
    }

    void request_stop() override
    {
        stop_requested_ = true;
        quit_program = true;
        SPCFLAGS_SET(SPCFLAG_BRK);
    }

    void request_attention() override
    {
        attention_ = true;
        /* Makes UAE look for interrupts at its next check (compiled code
         * included) and wakes the STOP loop; intlev() serves the
         * request. */
        SPCFLAGS_SET(SPCFLAG_INT | SPCFLAG_DOINT);
    }

    void set_irq(int level) override
    {
        if(level == 7)
            nmi_ = true;  /* an edge: the line keeps its level */
        else
            irq_ = level;
        if(level > 0)
            SPCFLAGS_SET(SPCFLAG_INT);
    }

    uint32_t pc() const override { return m68k_getpc(); }
    void set_pc(uint32_t pc) override
    {
        m68k_setpc(pc);
        fill_prefetch_0();
    }

    uint32_t reg(int id) const override
    {
        if(id >= m68k::D0 && id <= m68k::A7)
            return regs.regs[id - m68k::D0];
        switch(id)
        {
            case m68k::SR: MakeSR(); return regs.sr;
            case m68k::USP: return regs.s ? regs.usp : regs.regs[15];
            case m68k::ISP: return regs.s && !regs.m ? regs.regs[15] : regs.isp;
            case m68k::MSP: return regs.s && regs.m ? regs.regs[15] : regs.msp;
            case m68k::VBR: return regs.vbr;
            case m68k::SFC: return regs.sfc;
            case m68k::DFC: return regs.dfc;
            case m68k::CACR: return 0;
        }
        return 0;
    }

    void set_reg(int id, uint32_t value) override
    {
        if(id >= m68k::D0 && id <= m68k::A7)
        {
            regs.regs[id - m68k::D0] = value;
            return;
        }
        switch(id)
        {
            case m68k::SR:
                /* MakeFromSR switches stacks and flags a newly unmasked
                 * interrupt for after the current instruction. */
                MakeSR();
                regs.sr = (uae_u16)value;
                MakeFromSR();
                break;
            case m68k::USP: (regs.s ? regs.usp : regs.regs[15]) = value; break;
            case m68k::ISP: (regs.s && !regs.m ? regs.regs[15] : regs.isp) = value; break;
            case m68k::MSP: (regs.s && regs.m ? regs.regs[15] : regs.msp) = value; break;
            case m68k::VBR: regs.vbr = value; break;
            case m68k::SFC: regs.sfc = value; break;
            case m68k::DFC: regs.dfc = value; break;
            default: break;
        }
    }

    size_t context_size() const override { return sizeof(Context); }
    void save_context(void *dst) const override
    {
        Context *c = (Context *)dst;
        c->r = regs;
        c->f = regflags;
    }
    void restore_context(const void *src) override
    {
        const Context *c = (const Context *)src;
        regs = c->r;
        regflags = c->f;
        if(irq_ > 0)
            SPCFLAGS_SET(SPCFLAG_INT);
    }

    /* uae_host_hooks. */
    void emulop(uint16_t opcode)
    {
        /* UAE adds 2 to the PC after this returns; the Host sees the PC of
         * the next instruction, as on every core. */
        uaecptr op_pc = m68k_getpc();
        m68k_setpc(op_pc + 2);
        Host::OpResult r = host_.host_op(*this, opcode, op_pc);
        m68k_setpc(m68k_getpc() - 2);
        fill_prefetch_0();
        if(r == Host::OpResult::Stop)
            stop_innermost();
    }

    int intlev()
    {
        serve_attention();
        if(nmi_.exchange(false))
            return 7;
        return irq_ > 0 ? (int)irq_ : -1;
    }

    void poll()
    {
        serve_attention();
        if(irq_ > 0)
            SPCFLAGS_SET(SPCFLAG_INT);
    }

private:
    struct Context
    {
        regstruct r;
        flag_struct f;
    };

    /* The $7100 at pc: hand it to the Host. True when it stopped. */
    bool exit_op(uaecptr pc)
    {
        set_pc(pc + 2);
        if(host_.host_op(*this, m68k::HOST_OP_MATCH, pc) == Host::OpResult::Stop)
            return true;
        return false;
    }

    void stop_innermost()
    {
        if(stop_flag_)
            *stop_flag_ = true;
        quit_program = true;
        SPCFLAGS_SET(SPCFLAG_BRK);
    }

    void serve_attention()
    {
        if(attention_.exchange(false))
            host_.attention(*this);
    }

    Host &host_;
    std::atomic<int> irq_{0};
    std::atomic<bool> nmi_{false};
    int depth_ = 0;
    bool *stop_flag_ = nullptr;  /* the innermost run()'s */
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> attention_{false};
};

void emulop_hook(uint16_t opcode) { g_core->emulop(opcode); }
int intlev_hook(void) { return g_core->intlev(); }
void poll_hook(void) { g_core->poll(); }

}  // namespace

std::unique_ptr<Core> create_uae(const Config &config, GuestMemory &memory, Host &host)
{
    intptr_t delta;
    if(g_core || config.arch != Arch::M68K
       || config.model < 68000 || config.model > 68040 || !memory.uniform(&delta))
        return nullptr;
    if(platform_memory() != &memory)
    {
        fprintf(stderr, "uae: g_platform.mem_* must be this core's GuestMemory\n");
        return nullptr;
    }
    return std::unique_ptr<Core>(new UaeCore(config, delta, memory, host));
}

}  // namespace cpu

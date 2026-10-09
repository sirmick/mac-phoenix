/*
 * uae_core.cpp - cpu::Core on MacPhoenix's UAE interpreter (src/cpu/uae_cpu).
 *
 * UAE reaches the outside world through g_platform; while a UaeCore exists
 * it owns these entries (and puts the previous ones back when destroyed):
 *
 *   mem_*                  the GuestMemory, big-endian
 *   m68k_emulop_handler    host ops $7101-$71FF
 *   m68k_intlev            the interrupt line
 *   m68k_poll_interrupts   called from UAE's tick check: serves
 *                          request_attention() and re-raises the line
 *
 * $7100 is UAE's built-in EMULOP_RETURN: it leaves m68k_execute() with the
 * PC still on it. run() then hands it to the Host like any other host op.
 *
 * UAE fetches instructions through a host pointer (MEMBaseDiff), so the
 * GuestMemory must be one window or several with the same base offset
 * (GuestMemory::uniform); create_uae() returns null otherwise.
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

#include "cpu_core.h"

#include <atomic>
#include <cstdio>

extern bool quit_program;
extern uintptr MEMBaseDiff;
extern int CPUType, FPUType;

namespace cpu {
namespace {

class UaeCore;
UaeCore *g_core;
GuestMemory *g_mem;

uint8_t mem_rb(uint32_t a) { return g_mem->read8(a); }
uint16_t mem_rw(uint32_t a) { return g_mem->read16(a); }
uint32_t mem_rl(uint32_t a) { return g_mem->read32(a); }
void mem_wb(uint32_t a, uint8_t v) { g_mem->write8(a, v); }
void mem_ww(uint32_t a, uint16_t v) { g_mem->write16(a, v); }
void mem_wl(uint32_t a, uint32_t v) { g_mem->write32(a, v); }
uint8_t *mem_m2h(uint32_t a) { return g_mem->host(a); }
uint32_t mem_h2m(uint8_t *p)
{
    uint32_t a = 0;
    g_mem->guest(p, &a);
    return a;
}

bool emulop_hook(uint16_t opcode, bool is_primary);
int intlev_hook(void);
void poll_hook(void);

class UaeCore final : public Core
{
public:
    UaeCore(const Config &config, intptr_t delta, GuestMemory &memory, Host &host)
        : host_(host), saved_(g_platform)
    {
        g_core = this;
        g_mem = &memory;
        CPUType = config.model / 10 % 10;  /* 68040 -> 4 */
        FPUType = config.fpu ? 1 : 0;
        MEMBaseDiff = (uintptr)delta;

        g_platform.mem_read_byte = mem_rb;
        g_platform.mem_read_word = mem_rw;
        g_platform.mem_read_long = mem_rl;
        g_platform.mem_write_byte = mem_wb;
        g_platform.mem_write_word = mem_ww;
        g_platform.mem_write_long = mem_wl;
        g_platform.mem_mac_to_host = mem_m2h;
        g_platform.mem_host_to_mac = mem_h2m;
        g_platform.m68k_emulop_handler = emulop_hook;
        g_platform.m68k_intlev = intlev_hook;
        g_platform.m68k_poll_interrupts = poll_hook;
        g_platform.trap_handler = nullptr;   /* real A-line/F-line exceptions */

        init_m68k();
    }

    ~UaeCore() override
    {
        Platform &p = g_platform;
        p.mem_read_byte = saved_.mem_read_byte;
        p.mem_read_word = saved_.mem_read_word;
        p.mem_read_long = saved_.mem_read_long;
        p.mem_write_byte = saved_.mem_write_byte;
        p.mem_write_word = saved_.mem_write_word;
        p.mem_write_long = saved_.mem_write_long;
        p.mem_mac_to_host = saved_.mem_mac_to_host;
        p.mem_host_to_mac = saved_.mem_host_to_mac;
        p.m68k_emulop_handler = saved_.m68k_emulop_handler;
        p.m68k_intlev = saved_.m68k_intlev;
        p.m68k_poll_interrupts = saved_.m68k_poll_interrupts;
        p.trap_handler = saved_.trap_handler;
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
            m68k_execute();
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

    void request_attention() override { attention_ = true; }

    void set_irq(int level) override
    {
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

    /* g_platform hooks. */
    bool emulop(uint16_t opcode)
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
        return true;
    }

    int intlev() const { return irq_ > 0 ? (int)irq_ : -1; }

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
    Platform saved_;
    std::atomic<int> irq_{0};
    bool *stop_flag_ = nullptr;  /* the innermost run()'s */
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> attention_{false};
};

bool emulop_hook(uint16_t opcode, bool) { return g_core->emulop(opcode); }
int intlev_hook(void) { return g_core->intlev(); }
void poll_hook(void) { g_core->poll(); }

}  // namespace

std::unique_ptr<Core> create_uae(const Config &config, GuestMemory &memory, Host &host)
{
    intptr_t delta;
    if(g_core || config.arch != Arch::M68K || config.address_bits != 32
       || config.model < 68000 || config.model > 68040 || !memory.uniform(&delta))
        return nullptr;
    return std::unique_ptr<Core>(new UaeCore(config, delta, memory, host));
}

}  // namespace cpu

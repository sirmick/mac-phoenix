/*
 * musashi_core.cpp - cpu::Core on Musashi (src/cpu/musashi).
 *
 * Memory: Musashi calls m68k_read/write_memory_* for every access; they go
 * to the GuestMemory.
 *
 * Host ops ($71xx) are illegal instructions to Musashi; its illegal-
 * instruction callback hands them to the Host. By then Musashi has stepped
 * past the opcode and REG_PPC holds its address.
 *
 * run() executes short time slices. Between slices it serves
 * request_stop() and request_attention() and reloads the interrupt line,
 * which Musashi samples only when a slice starts and when the guest writes
 * SR. A Stop from a host op ends the slice (m68k_end_timeslice). Musashi's
 * cycle count is global, so a nested run() also ends the outer slice
 * early; the outer loop simply goes round again.
 *
 * Musashi's 68020-68040 always have an FPU; Config::fpu is not honoured.
 */
#include "cpu_core.h"

#include "m68k.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace cpu {
namespace {

/* Cycles per m68k_execute(): the worst-case latency of an interrupt,
 * request_stop() or request_attention(). */
const int SLICE_CYCLES = 4000;

class MusashiCore;
MusashiCore *g_core;
GuestMemory *g_mem;

int illegal_instruction(int opcode);

class MusashiCore final : public Core
{
public:
    MusashiCore(unsigned type, GuestMemory &memory, Host &host) : type_(type), host_(host)
    {
        g_core = this;
        g_mem = &memory;
        m68k_set_cpu_type(type);
        m68k_init();
        m68k_set_illg_instr_callback(illegal_instruction);
    }

    ~MusashiCore() override
    {
        g_core = nullptr;
        g_mem = nullptr;
    }

    const char *name() const override { return "musashi"; }
    Arch arch() const override { return Arch::M68K; }

    void reset() override { m68k_pulse_reset(); }

    StopReason run() override
    {
        bool stopped = false;
        bool *outer = stop_flag_;
        stop_flag_ = &stopped;
        StopReason why;
        for(;;)
        {
            if(attention_.exchange(false))
                host_.attention(*this);
            if(stop_requested_.exchange(false))
            {
                why = StopReason::Requested;
                break;
            }
            m68k_set_irq(irq_);
            m68k_execute(SLICE_CYCLES);
            if(stopped)
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
        m68k_set_irq(irq_);
        m68k_execute(1);
        stop_flag_ = outer;
    }

    void request_stop() override { stop_requested_ = true; }
    void request_attention() override { attention_ = true; }

    void set_irq(int level) override
    {
        irq_ = level;
        m68k_set_irq(level);  /* sampled at the next slice or SR write */
    }

    uint32_t pc() const override { return m68k_get_reg(nullptr, M68K_REG_PC); }
    void set_pc(uint32_t pc) override { m68k_set_reg(M68K_REG_PC, pc); }

    uint32_t reg(int id) const override
    {
        return m68k_get_reg(nullptr, musashi_reg(id));
    }

    void set_reg(int id, uint32_t value) override
    {
        if(id == m68k::SR)
        {
            /* Writing SR samples the interrupt line. Host code runs inside
             * an instruction and may be about to set the PC, so no
             * interrupt may be taken here: lower the line around it. */
            m68k_set_irq(0);
            m68k_set_reg(M68K_REG_SR, value);
            m68k_set_irq(irq_);
            return;
        }
        m68k_set_reg(musashi_reg(id), value);
    }

    size_t context_size() const override { return m68k_context_size(); }
    void save_context(void *dst) const override { m68k_get_context(dst); }
    void restore_context(const void *src) override
    {
        m68k_set_context(const_cast<void *>(src));
        m68k_set_irq(irq_);  /* the saved line is stale */
    }

    int disassemble(uint32_t addr, char *buf, size_t len) override
    {
        char tmp[256];
        int n = (int)m68k_disassemble(tmp, addr, type_);
        snprintf(buf, len, "%s", tmp);
        return n;
    }

    int host_op(int opcode)
    {
        uint32_t op_pc = m68k_get_reg(nullptr, M68K_REG_PPC);
        if(host_.host_op(*this, (uint32_t)opcode, op_pc) == Host::OpResult::Stop)
        {
            if(stop_flag_)
                *stop_flag_ = true;
            m68k_end_timeslice();
        }
        return 1;
    }

private:
    static m68k_register_t musashi_reg(int id)
    {
        if(id >= m68k::D0 && id <= m68k::A7)
            return (m68k_register_t)(M68K_REG_D0 + (id - m68k::D0));
        switch(id)
        {
            case m68k::SR: return M68K_REG_SR;
            case m68k::USP: return M68K_REG_USP;
            case m68k::ISP: return M68K_REG_ISP;
            case m68k::MSP: return M68K_REG_MSP;
            case m68k::VBR: return M68K_REG_VBR;
            case m68k::CACR: return M68K_REG_CACR;
            case m68k::SFC: return M68K_REG_SFC;
            case m68k::DFC: return M68K_REG_DFC;
        }
        fprintf(stderr, "musashi: no register %d\n", id);
        return M68K_REG_PPC;
    }

    unsigned type_;
    Host &host_;
    int irq_ = 0;
    bool *stop_flag_ = nullptr;  /* the innermost run()'s */
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> attention_{false};
};

int illegal_instruction(int opcode)
{
    if(!m68k::is_host_op((uint32_t)opcode) || !g_core)
    {
        /* A real illegal instruction: take the exception. Logged (the
         * first few) because it is often an instruction Musashi lacks. */
        static int logged;
        if(logged < 20)
        {
            logged++;
            fprintf(stderr, "musashi: illegal instruction %04x at %08x\n", opcode,
                    m68k_get_reg(nullptr, M68K_REG_PPC));
        }
        return 0;
    }
    return g_core->host_op(opcode);
}

}  // namespace

std::unique_ptr<Core> create_musashi(const Config &config, GuestMemory &memory, Host &host)
{
    if(g_core || config.arch != Arch::M68K)
        return nullptr;
    unsigned type;
    switch(config.model)
    {
        case 68000: type = M68K_CPU_TYPE_68000; break;
        case 68010: type = M68K_CPU_TYPE_68010; break;
        case 68020: type = config.address_bits == 24 ? M68K_CPU_TYPE_68EC020 : M68K_CPU_TYPE_68020; break;
        case 68030: type = M68K_CPU_TYPE_68030; break;
        case 68040: type = M68K_CPU_TYPE_68040; break;
        default: return nullptr;
    }
    /* 24-bit addressing only where Musashi masks to 24 bits itself. */
    if(config.address_bits == 24 && config.model > 68020)
        return nullptr;
    return std::unique_ptr<Core>(new MusashiCore(type, memory, host));
}

}  // namespace cpu

/* Musashi's memory interface. */
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return cpu::g_mem->read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return cpu::g_mem->read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return cpu::g_mem->read32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { cpu::g_mem->write8(a, (uint8_t)v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { cpu::g_mem->write16(a, (uint16_t)v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { cpu::g_mem->write32(a, v); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return cpu::g_mem->read8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return cpu::g_mem->read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return cpu::g_mem->read32(a); }
}

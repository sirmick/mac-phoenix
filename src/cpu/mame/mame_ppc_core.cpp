/*
 * mame_ppc_core.cpp - cpu::Core on MAME's PowerPC recompiler
 * (src/cpu/mame/powerpc on src/cpu/mame/cpu, the UML recompiler).
 *
 * The device is MAME's ppc750_device; the recompiler's back-end is the
 * UML interpreter (drcbec) until the native back-ends land (C2c, C2d).
 * Every guest access goes through the shim's address_space to the
 * GuestMemory; no fastram is registered.
 *
 * Host ops: primary opcode 6 (cpu::ppc::is_host_op). The lifted front end
 * describes it as a sequence-ending instruction that may touch anything;
 * the generated code sets m_core->pc to the next instruction, calls
 * ppc_host_op() (our override), then checks interrupts and cycles and
 * dispatches to whatever pc now holds.
 *
 * run() executes slices of SLICE_CYCLES, shortened to the next timer
 * deadline (the decrementer). The generated code checks the interrupt line
 * (irq_pending, MSR[EE]) and the cycle count at sequence boundaries, so a
 * set_irq() or request_stop() from a host op takes effect at the next
 * boundary; both also zero the cycle count to make that soon.
 *
 * Context: the register block internal_ppc_state, copied whole. TLB and
 * timers are not part of it.
 */
#include "emu.h"
#include "ppc.h"
#include "ppccom.h"
#include "ppc_dasm.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace cpu {
namespace {

const int SLICE_CYCLES = 20000;

/* A device holds a reference to its machine_config for life. */
machine_config g_mconfig;
const device_type_impl MAMEPPC_TYPE{"mame-ppc", "PowerPC 750 (MAME)"};

class MamePpcCore;

class MamePpcDevice final : public ppc750_device
{
public:
    MamePpcDevice(const Config &config, GuestMemory &memory)
        : ppc750_device(g_mconfig, "mame-ppc", nullptr, config.clock_hz ? config.clock_hz : 100000000),
          timers_(config.timers), exceptions_(config.exceptions)
    {
        shim_bind_memory(memory);
        /* Config::jit: the native back-end (x86-64 today) or the UML
         * interpreter. Read by drcuml_state's constructor in device_start. */
        machine().mutable_options().set_drc_use_c(!config.jit);
        ppc_set_translation_off(!config.mmu);
        /* Verify every opcode of a block before running it (the Mac ROM's
         * 68k emulator recycles its translation buffer), flush the PC
         * before memory accesses, accurate single-precision results. */
        ppcdrc_set_options(PPCDRC_COMPATIBLE_OPTIONS);
    }

    bool ppc_exception_hook(int exception) override
    {
        static int logged[EXCEPTION_COUNT];
        uint32_t srr0 = m_core->spr[SPROEA_SRR0], srr1 = m_core->spr[SPROEA_SRR1];
        if(exceptions_)
        {
            if(exception >= 0 && exception < EXCEPTION_COUNT && logged[exception]++ < 10)
                fprintf(stderr, "[mame-ppc] exception %d at %08x (srr1 %08x, msr %08x)\n", exception, srr0, srr1,
                        m_core->msr);
            return false;
        }
        /* No FPU available: Kheperix ignores MSR[FP], so turn it on and run
         * the instruction. */
        if(exception == EXCEPTION_NOFPU)
        {
            m_core->msr |= MSROEA_FP;
            m_core->pc = srr0;
            return true;
        }
        if(exception >= 0 && exception < EXCEPTION_COUNT && logged[exception]++ < 20)
            fprintf(stderr, "[mame-ppc] exception %d at %08x (srr1 %08x, msr %08x), skipped\n", exception, srr0,
                    srr1, m_core->msr);
        /* Skip the instruction: SRR0 is the faulting instruction except for
         * a system call, where it is already the next one. */
        m_core->pc = (exception == EXCEPTION_SYSCALL) ? srr0 : srr0 + 4;
        return true;
    }

    /* The decrementer: on the SheepShaver machine the host paces the
     * guest with its own tick and never lets the decrementer or an external
     * interrupt raise a PowerPC exception (Config::timers off). */
    TIMER_CALLBACK_MEMBER(decrementer_int_callback) override
    {
        if(timers_)
            ppc750_device::decrementer_int_callback(param);
    }

    std::unique_ptr<util::disasm_interface> create_disassembler() override
    {
        return std::make_unique<powerpc_disassembler>(powerpc_disassembler::I_POWERPC);
    }

    void ppc_host_op(uint32_t opcode) override;

    using State = internal_ppc_state; /* the register block, reachable by the adapter */
    void run_slice() { execute_run(); }
    State &core() { return *m_core; }
    const State &core() const { return *m_core; }
    void flush_code() { code_flush_cache(); }
    void invalidate_code(uint32_t addr, uint32_t len)
    {
        /* The recompiler validates each block's opcodes against memory
         * before running it (the checksum), so changed code is recompiled
         * on its own; a flush just makes that happen at once. */
        (void)addr; (void)len;
        set_cache_dirty();
    }

    /* get_cr/set_cr/get_xer/set_xer are inline in ppccom.cpp, so the same
     * compositions live here. */
    u32 cr_value() const
    {
        u32 v = 0;
        for(int i = 0; i < 8; i++)
            v |= (m_core->cr[i] & 0x0f) << (28 - 4 * i);
        return v;
    }
    void set_cr_value(u32 value)
    {
        for(int i = 0; i < 8; i++)
            m_core->cr[i] = (value >> (28 - 4 * i)) & 0x0f;
    }
    u32 xer_value() const { return m_core->spr[SPR_XER] | (m_core->xerso << 31); }
    void set_xer_value(u32 value)
    {
        m_core->spr[SPR_XER] = value & 0x7fffffffu;
        m_core->xerso = value >> 31;
    }

    u32 get_reg(int id) const
    {
        auto *self = const_cast<MamePpcDevice *>(this);
        if(id >= ppc::GPR0 && id <= ppc::GPR31)
            return m_core->r[id - ppc::GPR0];
        switch(id)
        {
            case ppc::CR: return cr_value();
            case ppc::LR: return m_core->spr[SPR_LR];
            case ppc::CTR: return m_core->spr[SPR_CTR];
            case ppc::XER: return xer_value();
            case ppc::MSR: return m_core->msr;
            case ppc::FPSCR: return m_core->fpscr;
            case ppc::SRR0: return m_core->spr[SPROEA_SRR0];
            case ppc::SRR1: return m_core->spr[SPROEA_SRR1];
            case ppc::DEC: return self->get_decrementer();
        }
        fprintf(stderr, "mame-ppc: no register %d\n", id);
        return 0;
    }

    void set_reg(int id, u32 value)
    {
        if(id >= ppc::GPR0 && id <= ppc::GPR31)
        {
            m_core->r[id - ppc::GPR0] = value;
            return;
        }
        switch(id)
        {
            case ppc::CR: set_cr_value(value); return;
            case ppc::LR: m_core->spr[SPR_LR] = value; return;
            case ppc::CTR: m_core->spr[SPR_CTR] = value; return;
            case ppc::XER: set_xer_value(value); return;
            case ppc::MSR: m_core->msr = value; return;
            case ppc::FPSCR: m_core->fpscr = value; return;
            case ppc::SRR0: m_core->spr[SPROEA_SRR0] = value; return;
            case ppc::SRR1: m_core->spr[SPROEA_SRR1] = value; return;
            case ppc::DEC: set_decrementer(value); return;
        }
        fprintf(stderr, "mame-ppc: no register %d\n", id);
    }

    MamePpcCore *core_adapter = nullptr;

private:
    bool timers_;
    bool exceptions_;
};

class MamePpcCore final : public Core
{
public:
    MamePpcCore(const Config &config, GuestMemory &memory, Host &host)
        : dev_(config, memory), host_(host), memory_(memory)
    {
        dev_.core_adapter = this;
        dev_.shim_start();
    }

    const char *name() const override { return "mame-ppc"; }
    Arch arch() const override { return Arch::PPC; }

    void reset() override { dev_.shim_reset(); }

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
            dev_.shim_run_timers();
            int slice = (int)dev_.shim_cycles_to_next_timer(SLICE_CYCLES);
            if(slice < 1)
                slice = 1;
            dev_.shim_begin_slice(slice);
            depth_++;
            dev_.run_slice();
            depth_--;
            dev_.shim_end_slice();
            if(trace_ && traced_ < 400)
            {
                traced_++;
                fprintf(stderr, "[mame-ppc] slice end depth=%d pc=%08x icount=%d stopped=%d\n", depth_, dev_.core().pc,
                        dev_.core().icount, (int)stopped);
            }
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
        dev_.shim_begin_slice(1);
        dev_.run_slice();
        dev_.shim_end_slice();
        stop_flag_ = outer;
    }

    void request_stop() override
    {
        stop_requested_ = true;
        dev_.core().icount = 0;
    }
    void request_attention() override
    {
        attention_ = true;
        dev_.core().icount = 0;
    }

    void set_irq(int level) override
    {
        dev_.set_input_line(PPC_IRQ, level ? ASSERT_LINE : CLEAR_LINE);
        if(dev_.core().icount > 0)
            dev_.core().icount = 0;
    }

    uint32_t pc() const override { return dev_.core().pc; }
    void set_pc(uint32_t pc) override { dev_.core().pc = pc; }
    uint32_t reg(int id) const override { return dev_.get_reg(id); }
    void set_reg(int id, uint32_t value) override { dev_.set_reg(id, value); }

    size_t context_size() const override { return sizeof(MamePpcDevice::State); }
    void save_context(void *dst) const override { memcpy(dst, &dev_.core(), sizeof(MamePpcDevice::State)); }
    void restore_context(const void *src) override { memcpy(&dev_.core(), src, sizeof(MamePpcDevice::State)); }

    void invalidate(uint32_t addr, uint32_t len) override { dev_.invalidate_code(addr, len); }

    int disassemble(uint32_t addr, char *buf, size_t len) override
    {
        struct Buffer final : util::disasm_interface::data_buffer
        {
            GuestMemory &m;
            explicit Buffer(GuestMemory &mem) : m(mem) {}
            u8 r8(offs_t pc) const override { return m.read8(pc); }
            u16 r16(offs_t pc) const override { return m.read16(pc); }
            u32 r32(offs_t pc) const override { return m.read32(pc); }
            u64 r64(offs_t pc) const override { return (u64)m.read32(pc) << 32 | m.read32(pc + 4); }
        } data(memory_);
        powerpc_disassembler dasm(powerpc_disassembler::I_POWERPC);
        std::ostringstream out;
        offs_t r = dasm.disassemble(out, addr, data, data);
        snprintf(buf, len, "%s", out.str().c_str());
        return (int)(r & util::disasm_interface::LENGTHMASK);
    }

    /* From the device: a primary-opcode-6 instruction at op_pc. */
    void host_op(uint32_t opcode)
    {
        uint32_t op_pc = dev_.core().pc - 4;
        if(host_.host_op(*this, opcode, op_pc) == Host::OpResult::Stop)
        {
            if(stop_flag_)
                *stop_flag_ = true;
            dev_.core().icount = 0;
            if(trace_ && traced_ < 400)
            {
                traced_++;
                fprintf(stderr, "[mame-ppc] host op stop: depth=%d op_pc=%08x pc now %08x\n", depth_, op_pc,
                        dev_.core().pc);
            }
        }
    }

private:
    MamePpcDevice dev_;
    Host &host_;
    GuestMemory &memory_;
    int depth_ = 0;
    int traced_ = 0;
    bool trace_ = getenv("MACEMU_MAMEPPC_TRACE") && *getenv("MACEMU_MAMEPPC_TRACE") != '0';
    bool *stop_flag_ = nullptr; /* the innermost run()'s */
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> attention_{false};
};

void MamePpcDevice::ppc_host_op(uint32_t opcode)
{
    if(core_adapter)
        core_adapter->host_op(opcode);
}

}  // namespace

std::unique_ptr<Core> create_mame_ppc(const Config &config, GuestMemory &memory, Host &host)
{
    if(config.arch != Arch::PPC)
        return nullptr;
    switch(config.model)
    {
        case 750:
        case 7400: /* a G4 runs as a 750 here: no AltiVec */
            break;
        default:
            return nullptr;
    }
    return std::unique_ptr<Core>(new MamePpcCore(config, memory, host));
}

}  // namespace cpu

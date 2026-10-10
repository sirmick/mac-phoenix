/*
 * mame_68k_core.cpp - cpu::Core on MAME's Musashi (src/cpu/mame/m68000).
 *
 * MAME's 68020-68040 core is Musashi as a class instance: its memory
 * accessors are std::function fields bound here to the GuestMemory
 * through the shim's address spaces, so several cores can exist in one
 * process. It brings the 030/040 PMMU (Config::mmu) and a softfloat FPU
 * (Config::fpu).
 *
 * Host ops ($71xx) are illegal instructions to the 68k; the core hooks
 * m68ki_host_op() (the one local change in m68kcpu.h) and hands them to
 * the Host. By then Musashi has fetched the opcode: m_ppc is its address,
 * m_pc the next instruction.
 *
 * run() executes short time slices (m_icount). Between slices it serves
 * request_stop() and request_attention(). Musashi samples the interrupt
 * line when a slice starts and when the guest writes SR; set_irq() from
 * inside a slice ends it so the new level is seen at once. A Stop from a
 * host op ends the slice. m_icount is per instance, so a nested run()
 * (host code calling guest code from a host op) ends only its own slice.
 *
 * Reset: MAME's device fetches SSP and PC from the reset vectors at the
 * start of the next execute_run(); reset() does that fetch itself so a
 * set_pc() after reset() is not undone, as the Core contract requires.
 */
#include "emu.h"
#include "m68kmusashi.h"
#include "m68kdasm.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace cpu {
namespace {

/* Cycles per execute_run(): the worst-case latency of an interrupt,
 * request_stop() or request_attention(). */
const int SLICE_CYCLES = 4000;

const device_type_impl MAME68K_TYPE{"mame-68k", "Motorola 680x0 (MAME Musashi)"};

class Mame68kCore;

/* The device: Musashi with its protected state reachable by the Core. */
class Mame68kDevice final : public m68000_musashi_device
{
public:
    Mame68kDevice(const Config &config, GuestMemory &memory)
        : m68000_musashi_device(machine_config{}, "mame-68k", nullptr, 0, MAME68K_TYPE,
                                config.model < 68020 ? 16 : 32,  /* data bus */
                                config.address_bits == 24 ? 24 : 32),
          config_(config)
    {
        shim_bind_memory(memory, m_cpu_space_id);
    }

    void device_start() override
    {
        m68000_musashi_device::device_start();
        switch(config_.model)
        {
            case 68000:
                init_cpu_m68000();
                break;
            case 68010:
                init_cpu_m68010();
                break;
            case 68020:
                if(config_.address_bits == 24)
                    init_cpu_m68ec020();
                else if(config_.fpu)
                    init_cpu_m68020fpu();
                else
                    init_cpu_m68020();
                break;
            case 68030:
                if(config_.mmu)
                    init_cpu_m68030();
                else
                    init_cpu_m68ec030();
                break;
            case 68040:
            default:
                if(config_.mmu)
                    init_cpu_m68040();
                else if(config_.fpu)
                    init_cpu_m68ec040();  /* 040 without MMU; the FPU is forced on below */
                else
                    init_cpu_m68lc040();
                break;
        }
        if(config_.model >= 68030)
            m_has_fpu = config_.fpu;
        if(!config_.mmu && config_.model >= 68030)
        {
            /* The *mmu inits bind translating accessors; rebind plain
             * ones so there is no per-access check. */
            m_has_pmmu = false;
            init32(*m_program, *m_oprogram);
        }
        define_state();
    }

    std::unique_ptr<util::disasm_interface> create_disassembler() override { return nullptr; }

    bool m68ki_host_op() override;
    bool m68ki_trap_op() override;
    void clear_icache() { m68ki_ic_clear(); }

    /* The Core reaches Musashi's state through these. */
    void run_slice() { execute_run(); }
    u32 get_pc() const { return m_pc; }
    void jump(u32 pc) { m68ki_jump(pc); }
    u32 get_ppc() const { return m_ppc; }
    u32 ir() const { return m_ir; }
    u32 get_sr() const { return const_cast<Mame68kDevice *>(this)->m68ki_get_sr(); }
    void set_sr_noint(u32 v) { m68ki_set_sr_noint(v); }
    int &icount() { return m_icount; }
    bool stopped() const { return m_stopped != 0; }

    u32 get_reg(int id) const
    {
        auto *self = const_cast<Mame68kDevice *>(this);
        if(id >= m68k::D0 && id <= m68k::A7)
            return m_dar[id - m68k::D0];
        switch(id)
        {
            case m68k::SR: return self->m68ki_get_sr();
            case m68k::USP: return !m_s_flag ? m_dar[15] : m_sp[0];
            case m68k::ISP: return (m_s_flag && !m_m_flag) ? m_dar[15] : m_sp[4];
            case m68k::MSP: return (m_s_flag && m_m_flag) ? m_dar[15] : m_sp[6];
            case m68k::VBR: return m_vbr;
            case m68k::CACR: return m_cacr;
            case m68k::SFC: return m_sfc;
            case m68k::DFC: return m_dfc;
        }
        fprintf(stderr, "mame-68k: no register %d\n", id);
        return 0;
    }

    void set_reg(int id, u32 value)
    {
        if(id >= m68k::D0 && id <= m68k::A7)
        {
            m_dar[id - m68k::D0] = value;
            return;
        }
        switch(id)
        {
            case m68k::SR: m68ki_set_sr_noint(value); return;
            case m68k::USP: (!m_s_flag ? m_dar[15] : m_sp[0]) = value; return;
            case m68k::ISP: ((m_s_flag && !m_m_flag) ? m_dar[15] : m_sp[4]) = value; return;
            case m68k::MSP: ((m_s_flag && m_m_flag) ? m_dar[15] : m_sp[6]) = value; return;
            case m68k::VBR: m_vbr = value; return;
            case m68k::CACR: m_cacr = value; return;
            case m68k::SFC: m_sfc = value & 7; return;
            case m68k::DFC: m_dfc = value & 7; return;
        }
        fprintf(stderr, "mame-68k: no register %d\n", id);
    }

    /* Reset, then fetch SSP and PC from the vectors now rather than at
     * the next execute_run(). */
    void reset_now()
    {
        device_reset();
        m_run_mode = RUN_MODE_BERR_AERR_RESET;
        REG_SP() = m68ki_read_imm_32();
        m_pc = m68ki_read_imm_32();
        m68ki_jump(m_pc);
        m_run_mode = RUN_MODE_NORMAL;
        m_reset_cycles = 0;
    }

    /* The interrupt line, Core semantics: one level 0..6 held, 7 is an
     * NMI edge that leaves the level as it was. */
    void set_line(int level)
    {
        if(level == 7)
        {
            set_irq_line(7, ASSERT_LINE); /* old != 7 -> nmi_pending */
            set_irq_line(7, CLEAR_LINE);
            return;
        }
        for(int l = 1; l <= 6; l++)
            set_irq_line(l, l == level ? ASSERT_LINE : CLEAR_LINE);
    }

    /* Everything a later resume needs (not memory, not the interrupt
     * line): the register file, flags, FPU, MMU and instruction cache. */
#define MAME68K_CONTEXT_FIELDS(X)                                                                 \
    X(m_dar) X(m_ppc) X(m_pc) X(m_sp) X(m_vbr) X(m_sfc) X(m_dfc) X(m_cacr) X(m_caar) X(m_ir)      \
    X(m_fpr) X(m_fpiar) X(m_fpsr) X(m_fpcr)                                                       \
    X(m_t1_flag) X(m_t0_flag) X(m_s_flag) X(m_m_flag) X(m_x_flag) X(m_n_flag) X(m_not_z_flag)     \
    X(m_v_flag) X(m_c_flag) X(m_int_mask) X(m_int_level) X(m_stopped) X(m_pref_addr)             \
    X(m_pref_data) X(m_instr_mode) X(m_run_mode) X(m_pmmu_enabled) X(m_hmmu_enabled)             \
    X(m_emmu_enabled) X(m_can_instruction_restart) X(m_fpu_just_reset) X(m_fpu_pending_exception) \
    X(m_fpu_frame) X(m_restart_instruction) X(m_tracing) X(m_address_error) X(m_aerr_address)    \
    X(m_aerr_write_mode) X(m_aerr_fc) X(m_virq_state) X(m_nmi_pending)                            \
    X(m_mmu_crp_aptr) X(m_mmu_crp_limit) X(m_mmu_srp_aptr) X(m_mmu_srp_limit) X(m_mmu_urp_aptr)   \
    X(m_mmu_tc) X(m_mmu_sr) X(m_mmu_sr_040) X(m_mmu_atc_tag) X(m_mmu_atc_data) X(m_mmu_atc_rr)    \
    X(m_mmu_tt0) X(m_mmu_tt1) X(m_mmu_itt0) X(m_mmu_itt1) X(m_mmu_dtt0) X(m_mmu_dtt1)             \
    X(m_mmu_acr0) X(m_mmu_acr1) X(m_mmu_acr2) X(m_mmu_acr3) X(m_mmu_last_page_entry)              \
    X(m_mmu_last_page_entry_addr) X(m_mmu_tmp_sr) X(m_mmu_tmp_fc) X(m_mmu_tmp_rw) X(m_mmu_tmp_sz) \
    X(m_mmu_tmp_buserror_address) X(m_mmu_tmp_buserror_occurred) X(m_mmu_tmp_buserror_fc)         \
    X(m_mmu_tmp_buserror_rw) X(m_mmu_tmp_buserror_sz) X(m_mmu_tablewalk)                          \
    X(m_mmu_last_logical_addr) X(m_ic_address) X(m_ic_data) X(m_ic_valid)

    size_t context_size() const
    {
        size_t n = 0;
#define X(f) n += sizeof(f);
        MAME68K_CONTEXT_FIELDS(X)
#undef X
        return n;
    }
    void save_context(void *dst) const
    {
        char *p = (char *)dst;
#define X(f) memcpy(p, &f, sizeof(f)); p += sizeof(f);
        MAME68K_CONTEXT_FIELDS(X)
#undef X
    }
    void restore_context(const void *src)
    {
        const char *p = (const char *)src;
#define X(f) memcpy(&f, p, sizeof(f)); p += sizeof(f);
        MAME68K_CONTEXT_FIELDS(X)
#undef X
    }

    Mame68kCore *core = nullptr;

private:
    Config config_;
};

class Mame68kCore final : public Core
{
public:
    Mame68kCore(const Config &config, GuestMemory &memory, Host &host)
        : dev_(config, memory), host_(host), memory_(memory)
    {
        dev_.core = this;
        dev_.shim_start();
    }

    const char *name() const override { return "mame-68k"; }
    Arch arch() const override { return Arch::M68K; }

    void reset() override { dev_.reset_now(); }

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
            dev_.icount() = SLICE_CYCLES;
            dev_.run_slice();
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
        dev_.icount() = 1;
        dev_.run_slice();
        stop_flag_ = outer;
    }

    void request_stop() override
    {
        stop_requested_ = true;
        dev_.icount() = 0; /* benign if read mid-instruction: ends the slice */
    }
    void request_attention() override
    {
        attention_ = true;
        dev_.icount() = 0;
    }

    void set_irq(int level) override
    {
        irq_ = level;
        dev_.set_line(level);
        /* Sampled when the next slice starts: make that now. */
        if(dev_.icount() > 0)
            dev_.icount() = 0;
    }

    uint32_t pc() const override { return dev_.get_pc(); }
    void set_pc(uint32_t pc) override { dev_.jump(pc); }
    uint32_t reg(int id) const override { return dev_.get_reg(id); }
    void set_reg(int id, uint32_t value) override { dev_.set_reg(id, value); }

    size_t context_size() const override { return dev_.context_size(); }
    void save_context(void *dst) const override { dev_.save_context(dst); }
    void restore_context(const void *src) override
    {
        dev_.restore_context(src);
        dev_.set_line(irq_); /* the saved line is stale */
    }

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
        m68k_disassembler dasm(m68k_disassembler::TYPE_68040);
        std::ostringstream out;
        offs_t r = dasm.disassemble(out, addr, data, data);
        snprintf(buf, len, "%s", out.str().c_str());
        return (int)(r & util::disasm_interface::LENGTHMASK);
    }

    /* Guest code changed: the 68020+ instruction cache must not serve the
     * old words. */
    void invalidate(uint32_t addr, uint32_t len) override
    {
        (void)addr; (void)len;
        dev_.clear_icache();
    }

    /* From the device, on an A-line or F-line word. */
    bool trap_op() { return host_.trap(*this, dev_.ir(), dev_.get_ppc()); }

    /* From the device, on a $71xx opcode. */
    bool host_op()
    {
        uint32_t opcode = dev_.ir();
        if(!m68k::is_host_op(opcode))
        {
            static int logged;
            if(logged < 20)
            {
                logged++;
                fprintf(stderr, "mame-68k: illegal instruction %04x at %08x\n", opcode, dev_.get_ppc());
            }
            return false;
        }
        if(host_.host_op(*this, opcode, dev_.get_ppc()) == Host::OpResult::Stop)
        {
            if(stop_flag_)
                *stop_flag_ = true;
            dev_.icount() = 0;
        }
        return true;
    }

private:
    Mame68kDevice dev_;
    Host &host_;
    GuestMemory &memory_;
    int irq_ = 0;
    bool *stop_flag_ = nullptr; /* the innermost run()'s */
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> attention_{false};
};

bool Mame68kDevice::m68ki_host_op()
{
    return core && core->host_op();
}

bool Mame68kDevice::m68ki_trap_op()
{
    return core && core->trap_op();
}

}  // namespace

std::unique_ptr<Core> create_mame_68k(const Config &config, GuestMemory &memory, Host &host)
{
    if(config.arch != Arch::M68K)
        return nullptr;
    switch(config.model)
    {
        case 68000:
        case 68010:
        case 68020:
        case 68030:
        case 68040:
            break;
        default:
            return nullptr;
    }
    /* 24-bit addressing: the 68000/68010 and the EC020 (the 24-bit mask
     * is the GuestMemory's; Musashi's own masking is per model). */
    if(config.address_bits == 24 && config.model > 68020)
        return nullptr;
    return std::unique_ptr<Core>(new Mame68kCore(config, memory, host));
}

}  // namespace cpu

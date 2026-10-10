/*
 * cpu_core.h - one interface for every CPU core MacPhoenix runs.
 *
 * A core executes guest instructions; everything else is the host's. The
 * same interface fits an interpreter, a JIT, a 68k or a PowerPC:
 *
 *   GuestMemory  the 32-bit guest address space: 64KB pages, each mapped
 *                to host memory (host = guest + delta) or sent to an I/O
 *                handler. The host builds it; the core reads through it.
 *   Host         what the core calls back: host_op() for an instruction in
 *                the architecture's host-call space, attention() at an
 *                instruction boundary after request_attention().
 *   Core         reset, run until stopped, step, interrupt line, registers,
 *                context save/restore for process switching.
 *
 * Returning to the host: by convention one host op means "return to
 * host" ($7100 on 68k). Its handler leaves the PC on the op and returns
 * Stop; run_until() and the Platform glue (platform_cpu.h) rely on that.
 *
 * Re-entrancy: run() may be called again from inside host_op() or
 * attention() (the host calling guest code, e.g. a Toolbox trap running a
 * definition procedure). A Stop from host_op() ends only the innermost
 * run(). The usual "call guest code at ADDR until it returns" is: push or
 * load a return address that holds a host op, set_pc(ADDR), run() until
 * that host op returns Stop (see run_until()).
 *
 * Threads: a core belongs to the thread that runs it. request_stop() and
 * request_attention() may be called from any thread; everything else only
 * from the core's thread (inside run() means inside a callback).
 *
 * Cores in this tree keep global state (UAE, Musashi): at most one of each
 * exists per process. create() returns null when the core is already in
 * use.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cpu {

enum class Arch { M68K, PPC };

/* ---------------------------------------------------------------------- */
/* Guest memory                                                           */
/* ---------------------------------------------------------------------- */

class GuestMemory
{
public:
    static constexpr int PAGE_SHIFT = 16;                       /* 64KB pages */
    static constexpr uint32_t PAGE_SIZE = 1u << PAGE_SHIFT;
    static constexpr uint32_t NUM_PAGES = 1u << (32 - PAGE_SHIFT);

    /* Accesses to unmapped pages. Without one, reads return 0 and writes
     * are dropped. size is 1, 2 or 4; values are in guest byte order
     * already decoded (big-endian guests: the number, not the bytes). */
    struct Io
    {
        virtual ~Io() = default;
        virtual uint32_t read(uint32_t addr, int size) = 0;
        virtual void write(uint32_t addr, uint32_t value, int size) = 0;
    };

    GuestMemory();

    /* Map [guest, guest + size) to host memory starting at host. guest
     * and size must be page-aligned; size may be 4GB (the whole space). A
     * window is contiguous on the host. */
    void map(uint32_t guest, uint64_t size, uint8_t *host);
    void unmap(uint32_t guest, uint64_t size);
    void set_io(Io *io) { io_ = io; }
    /* A 24-bit 68k ignores the top byte of every address (the Memory
     * Manager keeps flags there). Applied to every access and lookup. */
    void set_address_bits(int bits) { mask_ = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1; }

    /* Host pointer for a guest address, or nullptr when unmapped. */
    uint8_t *host(uint32_t addr) const
    {
        addr &= mask_;
        intptr_t d = delta_[addr >> PAGE_SHIFT];
        return d == UNMAPPED ? nullptr : (uint8_t *)(d + (intptr_t)addr);
    }
    /* Guest address of a host pointer inside a mapped window, or false. */
    bool guest(const void *host, uint32_t *addr) const;
    /* True when every mapped page has the same delta (one base offset):
     * what cores that fetch through a single base pointer need. */
    bool uniform(intptr_t *delta) const;

    /* Big-endian data accesses (the only kind so far; a little-endian
     * guest gets its own accessors). */
    uint8_t read8(uint32_t a) const
    {
        a &= mask_;
        const uint8_t *p = host(a);
        return p ? *p : (uint8_t)io_read(a, 1);
    }
    uint16_t read16(uint32_t a) const
    {
        a &= mask_;
        const uint8_t *p = host(a);
        if(p && (a & (PAGE_SIZE - 1)) <= PAGE_SIZE - 2)
            return (uint16_t)(p[0] << 8 | p[1]);
        return (uint16_t)slow_read(a, 2);
    }
    uint32_t read32(uint32_t a) const
    {
        a &= mask_;
        const uint8_t *p = host(a);
        if(p && (a & (PAGE_SIZE - 1)) <= PAGE_SIZE - 4)
            return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        return slow_read(a, 4);
    }
    void write8(uint32_t a, uint8_t v)
    {
        a &= mask_;
        uint8_t *p = host(a);
        if(p)
            *p = v;
        else
            io_write(a, v, 1);
    }
    void write16(uint32_t a, uint16_t v)
    {
        a &= mask_;
        uint8_t *p = host(a);
        if(p && (a & (PAGE_SIZE - 1)) <= PAGE_SIZE - 2)
        {
            p[0] = v >> 8;
            p[1] = (uint8_t)v;
        }
        else
            slow_write(a, v, 2);
    }
    void write32(uint32_t a, uint32_t v)
    {
        a &= mask_;
        uint8_t *p = host(a);
        if(p && (a & (PAGE_SIZE - 1)) <= PAGE_SIZE - 4)
        {
            p[0] = v >> 24;
            p[1] = (uint8_t)(v >> 16);
            p[2] = (uint8_t)(v >> 8);
            p[3] = (uint8_t)v;
        }
        else
            slow_write(a, v, 4);
    }

private:
    static constexpr intptr_t UNMAPPED = INTPTR_MIN;
    struct Window { uint32_t guest; uint64_t size; uint8_t *host; };
    intptr_t delta_[NUM_PAGES];
    std::vector<Window> windows_;  /* for guest(): few and large */
    uint32_t mask_ = 0xFFFFFFFFu;
    Io *io_ = nullptr;

    uint32_t io_read(uint32_t a, int size) const;
    void io_write(uint32_t a, uint32_t v, int size);
    /* An access crossing into another page, or unmapped. */
    uint32_t slow_read(uint32_t a, int size) const;
    void slow_write(uint32_t a, uint32_t v, int size);
};

/* ---------------------------------------------------------------------- */
/* Registers                                                              */
/* ---------------------------------------------------------------------- */

/* Register ids for Core::reg() / set_reg(), per architecture. Integer and
 * control registers, 32 bits. Floating-point state travels with the
 * context (save_context) until something needs it register by register. */
namespace m68k {
enum Reg : int {
    D0, D1, D2, D3, D4, D5, D6, D7,
    A0, A1, A2, A3, A4, A5, A6, A7,   /* A7: the active stack pointer */
    SR,                               /* full status register, with CCR */
    USP, ISP, MSP, VBR, CACR, SFC, DFC,
    NUM_REGS
};
/* Host calls: line 7 with bit 8 set ($71xx), illegal on every 68k
 * (MOVEQ needs bit 8 clear). $7100 is "return to host" by convention
 * (Basilisk II's M68K_EXEC_RETURN, Executor's exit cell); the host decides
 * what each one means. */
constexpr uint16_t HOST_OP_MASK = 0xFF00;
constexpr uint16_t HOST_OP_MATCH = 0x7100;
constexpr uint16_t HOST_OP_RETURN = 0x7100;  /* "return to host" */
inline bool is_host_op(uint32_t opcode) { return (opcode & HOST_OP_MASK) == HOST_OP_MATCH; }
}  // namespace m68k

namespace ppc {
enum Reg : int {
    GPR0, GPR31 = GPR0 + 31,
    CR, LR, CTR, XER, MSR, FPSCR, SRR0, SRR1, DEC,
    NUM_REGS
};
/* Host calls: SheepShaver's encoding, primary opcode 6 (unused on every
 * PowerPC), the low bits a selector. */
constexpr uint32_t HOST_OP_MASK = 0xFC000000;
constexpr uint32_t HOST_OP_MATCH = 0x18000000;
inline bool is_host_op(uint32_t opcode) { return (opcode & HOST_OP_MASK) == HOST_OP_MATCH; }
}  // namespace ppc

/* ---------------------------------------------------------------------- */
/* Host callbacks                                                         */
/* ---------------------------------------------------------------------- */

class Core;

class Host
{
public:
    virtual ~Host() = default;

    enum class OpResult { Continue, Stop };
    /* An instruction from the architecture's host-op space (m68k:: /
     * ppc::is_host_op) executed at op_pc. On entry pc() is the next
     * instruction; set_pc() to go elsewhere. Stop ends the innermost
     * run(). */
    virtual OpResult host_op(Core &core, uint32_t opcode, uint32_t op_pc) = 0;

    /* After request_attention(), at an instruction boundary on the core's
     * thread: the place to update the interrupt line or run guest code
     * (SheepShaver-style interrupt dispatch). */
    virtual void attention(Core &core) { (void)core; }

    /* m68k: an A-line or F-line word, before the exception is taken. True
     * means the host handled it: the word counts as executed and the core
     * goes on at pc() (just past it), unless the host moved the PC. False:
     * the normal exception (the Mac's trap dispatcher). Only cores that
     * implement it call it (mame-68k). */
    virtual bool trap(Core &core, uint32_t opcode, uint32_t op_pc)
    {
        (void)core; (void)opcode; (void)op_pc;
        return false;
    }
};

/* ---------------------------------------------------------------------- */
/* Cores                                                                  */
/* ---------------------------------------------------------------------- */

struct Config
{
    Arch arch = Arch::M68K;
    /* m68k: 68000, 68010, 68020, 68030, 68040.
     * ppc: 601, 603, 604, 750 (G3), 7400 (G4). */
    int model = 68040;
    bool fpu = true;
    /* m68k: emulate the PMMU (68030/68040); off means MMU instructions
     * are no-ops and addresses are never translated, as every machine
     * here has run so far. Only the MAME core honours it. */
    bool mmu = false;
    /* 24 for a 24-bit 68k Mac; the core masks addresses itself. */
    int address_bits = 32;
    bool jit = false;
    /* The CPU clock the core's timers run at (ppc: timebase and
     * decrementer); 0 means the core's default. */
    uint32_t clock_hz = 0;
    /* ppc: let the decrementer raise its exception (off: the host paces
     * the guest itself, as SheepShaver's machine does). */
    bool timers = true;
    /* ppc: deliver exceptions (program, DSI, ISI, alignment, system call)
     * through the vectors. Off: skip the instruction and log, as Kheperix
     * does; what SheepShaver's machine side expects. */
    bool exceptions = true;
};

enum class StopReason {
    HostOp,     /* host_op() returned Stop */
    Requested,  /* request_stop() */
    Halted,     /* the CPU halted (double fault, STOP with nothing to wake it) */
};

class Core
{
public:
    virtual ~Core() = default;

    virtual const char *name() const = 0;
    virtual Arch arch() const = 0;

    /* Power-on state: supervisor mode, interrupts masked, caches off.
     * Whether a 68k core also loads SSP and PC from the reset vectors
     * differs (Musashi does, UAE does not): set them afterwards. */
    virtual void reset() = 0;

    /* Run until a host op stops it, request_stop(), or a halt. */
    virtual StopReason run() = 0;
    /* Exactly one instruction (an exception or interrupt taken instead
     * counts as one). Host ops behave as in run(). */
    virtual void step() = 0;

    /* Any thread. run() returns Requested soon after. */
    virtual void request_stop() = 0;
    /* Any thread. Host::attention() runs soon, at an instruction boundary,
     * on the core's thread. Several requests may become one call. */
    virtual void request_attention() = 0;

    /* The interrupt line. m68k: the priority level 0..6 on IPL0-2,
     * level-sensitive (it stays until the host lowers it); 7 is the
     * non-maskable edge: one NMI, and the line keeps its previous level.
     * ppc: nonzero asserts the external interrupt. */
    virtual void set_irq(int level) = 0;

    virtual uint32_t pc() const = 0;
    virtual void set_pc(uint32_t pc) = 0;
    virtual uint32_t reg(int id) const = 0;
    virtual void set_reg(int id, uint32_t value) = 0;

    /* Everything the core needs to resume later on the same thread or
     * another: registers, FPU, MMU, pending exception state. Not memory,
     * not the interrupt line (the host owns those). */
    virtual size_t context_size() const = 0;
    virtual void save_context(void *dst) const = 0;
    virtual void restore_context(const void *src) = 0;

    /* Guest code in [addr, addr + len) changed (JITs and decoded-block
     * caches drop it). */
    virtual void invalidate(uint32_t addr, uint32_t len) { (void)addr; (void)len; }

    /* One instruction at addr into buf; returns its length in bytes, or 0
     * when the core has no disassembler. */
    virtual int disassemble(uint32_t addr, char *buf, size_t len)
    {
        (void)addr; (void)buf; (void)len;
        return 0;
    }

    /* Run from addr until a host op at return_pc stops it and the PC is
     * back there (an interrupt can divert the guest after the stop). The
     * caller has arranged for the guest to return to return_pc. */
    void run_until(uint32_t addr, uint32_t return_pc)
    {
        set_pc(addr);
        while(pc() != return_pc)
            run();
    }
};

/* Cores built into this binary for an architecture, best first. */
std::vector<std::string> available(Arch arch);

/* A core by name ("uae", "musashi", ...), or null if unknown, unsuitable
 * for the config, or already in use. memory and host must outlive it. */
std::unique_ptr<Core> create(const std::string &name, const Config &config,
                             GuestMemory &memory, Host &host);

}  // namespace cpu

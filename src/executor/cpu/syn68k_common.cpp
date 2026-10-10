/*
 * syn68k_common.cpp - the syn68k API on a cpu::Core (src/cpu/core).
 *
 * See syn68k_public.h for the model. In short:
 *   - identity addressing (guest == host, all below 4GB): the GuestMemory
 *     is one window over the whole 32-bit space at host 0,
 *   - callbacks are 2-byte cells in callback_dummy_address_space holding
 *     host op $7101; the core hands it to SynHost, which runs the callback
 *     the cell belongs to,
 *   - exceptions go through real guest vectors (VBR), each pointing at a
 *     forwarding callback, exactly as syn68k's trap.c did,
 *   - CALL_EMULATOR runs guest code until it returns into the exit cell
 *     (host op $7100, which stops the core's run()),
 *   - the core's interrupt line follows cpu_state.interrupt_pending[],
 *     updated on the core's thread (after callbacks, and from
 *     Host::attention() when another thread raised a level).
 *
 * The core is chosen by name before initialize_68k_emulator: "uae" or
 * "musashi" (cpu::available).
 */
#include <syn68k_public.h>
#include "cpu_core.h"
#include "platform_cpu.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#define MAX_CALLBACKS 16384

/* Static so it links below 4GB with the rest of the binary's data. */
uint16 callback_dummy_address_space[MAX_CALLBACKS + CALLBACK_SLOP];

#define CALLBACK_STUB_BASE (MAGIC_ADDRESS_BASE + 2 * sizeof(uint16))
#define FIRST_CALLBACK_CELL (CALLBACK_SLOP + 2)

struct CallbackInfo {
    callback_handler_t func;
    void *arg;
};
static CallbackInfo callbacks[MAX_CALLBACKS];
static uint32 num_callback_slots = 0;
static uint32 lowest_free_slot = 0;

/* ---------------------------------------------------------------------- */
/* Globals the syn68k API exposes                                         */
/* ---------------------------------------------------------------------- */

CPUState cpu_state;
/* Identity until the first allocation picks the windows (see Memory). */
uint64 ROMlib_offsets[OFFSET_TABLE_SIZE] = { 0, 0, 0, 0 };
uint64 ROMlib_sizes[OFFSET_TABLE_SIZE] = { 1ull << 30, 1ull << 30, 1ull << 30, 1ull << 30 };
DebuggerCallbacks syn68k_debugger_callbacks = { nullptr, nullptr };
int syn68k_track_pc = 0;
int emulation_depth = 0;

static const uint16 OP_EXEC_RETURN = 0x7100;  /* the exit cell: stop run() */
static const uint16 OP_CALLBACK    = 0x7101;  /* dispatch callback by PC   */

static cpu::GuestMemory guest_memory;
/* Never destroyed: the timer thread may still raise interrupts while the
 * process exits. */
static cpu::Core *core;
static std::string engine_name = "uae";

int syn68k_select_engine(const char *name)
{
    std::string n = name && *name ? name : "uae";
    if(core)
        return 0;
    for(const auto &a : cpu::available(cpu::Arch::M68K))
        if(a == n)
        {
            engine_name = n;
            return 1;
        }
    return 0;
}

const char *syn68k_engine_name(void)
{
    return engine_name.c_str();
}

/* ---------------------------------------------------------------------- */
/* Status register                                                        */
/* ---------------------------------------------------------------------- */

static uint16 syn68k_full_sr(void)
{
    return cpu_state.sr | (cpu_state.ccc ? 1 : 0) | (cpu_state.ccv ? 2 : 0)
         | (!cpu_state.ccnz ? 4 : 0) | (cpu_state.ccn ? 8 : 0)
         | (cpu_state.ccx ? 16 : 0);
}

static void syn68k_set_full_sr(uint16 sr)
{
    cpu_state.sr = sr & ~0x1F;
    cpu_state.ccc = sr & 1;
    cpu_state.ccv = sr & 2;
    cpu_state.ccnz = !(sr & 4);
    cpu_state.ccn = sr & 8;
    cpu_state.ccx = sr & 16;
}

/* ---------------------------------------------------------------------- */
/* Callbacks                                                              */
/* ---------------------------------------------------------------------- */

static inline uint32 slot_of(syn68k_addr_t addr)
{
    return (addr - CALLBACK_STUB_BASE) / sizeof(uint16);
}

syn68k_addr_t callback_install(callback_handler_t func, void *arbitrary_argument)
{
    uint32 slot = lowest_free_slot;
    if(slot >= num_callback_slots)
    {
        if(num_callback_slots >= MAX_CALLBACKS - FIRST_CALLBACK_CELL)
        {
            fprintf(stderr, "syn68k: out of callback slots\n");
            abort();
        }
        ++num_callback_slots;
    }
    callbacks[slot].func = func;
    callbacks[slot].arg = arbitrary_argument;

    for(lowest_free_slot++; lowest_free_slot < num_callback_slots
                            && callbacks[lowest_free_slot].func; lowest_free_slot++)
        ;
    return CALLBACK_STUB_BASE + slot * sizeof(uint16);
}

void callback_remove(syn68k_addr_t addr)
{
    uint32 slot = slot_of(addr);
    if(slot < num_callback_slots)
    {
        callbacks[slot].func = nullptr;
        callbacks[slot].arg = nullptr;
        if(slot < lowest_free_slot)
            lowest_free_slot = slot;
    }
}

void *callback_argument(syn68k_addr_t addr)
{
    uint32 slot = slot_of(addr);
    return slot < num_callback_slots ? callbacks[slot].arg : nullptr;
}

callback_handler_t callback_function(syn68k_addr_t addr)
{
    uint32 slot = slot_of(addr);
    return slot < num_callback_slots ? callbacks[slot].func : nullptr;
}

static syn68k_addr_t dispatch_callback(syn68k_addr_t pc)
{
    uint32 slot = slot_of(pc);
    if(pc < CALLBACK_STUB_BASE || slot >= num_callback_slots || !callbacks[slot].func)
    {
        fprintf(stderr, "syn68k: jump to dead callback at %08x\n", pc);
        abort();
    }
    return callbacks[slot].func(pc, callbacks[slot].arg);
}

static bool in_callback_page(syn68k_addr_t pc)
{
    syn68k_addr_t lo = US_TO_SYN68K(&callback_dummy_address_space[0]);
    syn68k_addr_t hi = US_TO_SYN68K(&callback_dummy_address_space[MAX_CALLBACKS + CALLBACK_SLOP]);
    return pc >= lo && pc < hi;
}

/* ---------------------------------------------------------------------- */
/* Exceptions and traps                                                   */
/* ---------------------------------------------------------------------- */

static const char *trap_description(int n)
{
    switch(n)
    {
        case 2: return "Bus error";
        case 3: return "Address error";
        case 4: return "Illegal instruction";
        case 5: return "Integer divide by zero";
        case 6: return "CHK/CHK2";
        case 7: return "TRAPcc/TRAPV";
        case 8: return "Privilege violation";
        case 9: return "Trace";
        case 10: return "A-line";
        case 11: return "F-line";
        case 14: return "Format error";
        default: return nullptr;
    }
}

/* Every vector points here first. Hands the handler the stacked PC. */
static syn68k_addr_t trap_forwarded(syn68k_addr_t, void *arg)
{
    const volatile TrapHandlerInfo *thi = (const volatile TrapHandlerInfo *)arg;
    syn68k_addr_t pc = READUL(EM_A7 + 2);
    if(!thi->func)
    {
        int n = thi - cpu_state.trap_handler_info;
        /* 68040 cache and ATC maintenance (CINV, CPUSH, PFLUSH): one word,
         * nothing to do on an emulator. Cores that don't implement them
         * (Musashi) raise an F-line exception; step over the instruction. */
        if(n == 11 && (READUW(pc) & 0xFE00) == 0xF400)
        {
            WRITEUL(EM_A7 + 2, pc + 2);
            return MAGIC_RTE_ADDRESS;
        }
        const char *d = trap_description(n);
        fprintf(stderr, "Unhandled trap %d%s%s%s at pc %08x\n", n,
                d ? " (" : "", d ? d : "", d ? ")" : "", pc);
        return MAGIC_RTE_ADDRESS;
    }
    return thi->func(pc, thi->arg);
}

void trap_install_handler(unsigned trap_number, callback_handler_t func,
                          void *arbitrary_argument)
{
    cpu_state.trap_handler_info[trap_number].func = func;
    cpu_state.trap_handler_info[trap_number].arg = arbitrary_argument;
}

void trap_remove_handler(unsigned trap_number)
{
    cpu_state.trap_handler_info[trap_number].func = nullptr;
}

/* ---------------------------------------------------------------------- */
/* Interrupts                                                             */
/* ---------------------------------------------------------------------- */

/*
 * The per-level bytes cpu_state.interrupt_pending[] are the only source of
 * truth. Producers (other threads, signal handlers) set a byte, then flag
 * the status. Consumers (Executor's Interrupt::handleCommon) clear the byte.
 * Nothing here clears a byte, so a concurrent interrupt_generate() can never
 * be lost; the status word is only a hint and is re-checked after clearing.
 */
static int highest_pending(void)
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for(int l = 7; l >= 1; l--)
        if(cpu_state.interrupt_pending[l])
            return l;
    return -1;
}

static void settle_interrupt_status()
{
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);
    if(highest_pending() > 0)
        SET_INTERRUPT_STATUS(INTERRUPT_STATUS_CHANGED);
}

/* The level for the core's interrupt line. */
static int irq_level()
{
    int l = highest_pending();
    return l > 0 ? l : 0;
}

void interrupt_generate(unsigned priority)
{
    cpu_state.interrupt_pending[priority] = 1;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_CHANGED);
    /* Other threads never touch the line; the core's thread updates it. */
    if(cpu::Core *c = core)
        c->request_attention();
}

void interrupt_note_if_present(void)
{
    if(highest_pending() > 0 && core)
        core->request_attention();
}

/* syn68k idiom used by host code that polls for interrupts. */
syn68k_addr_t interrupt_process_any_pending(syn68k_addr_t pc)
{
    int level = highest_pending();
    int mask = (cpu_state.sr >> 8) & 7;
    if(level < 0 || (level <= mask && level != 7))
    {
        settle_interrupt_status();
        return pc;
    }
    uint16 old_sr = syn68k_full_sr();
    uint32 vector = 24 + level;
    /* 68040 format-0 frame: SR, PC, format/vector word. */
    PUSHUW(vector * 4);
    PUSHUL(pc);
    PUSHUW(old_sr);
    cpu_state.sr = (cpu_state.sr & ~0x0700) | (level << 8) | 0x2000;
    return READUL(cpu_state.vbr + vector * 4);
}

/* ---------------------------------------------------------------------- */
/* The core                                                               */
/* ---------------------------------------------------------------------- */

/* The register file lives in cpu_state while host code runs and in the
 * core while guest code runs. */
static void core_to_cpu_state()
{
    for(int i = 0; i < 16; i++)
        cpu_state.regs[i].ul.n = core->reg(cpu::m68k::D0 + i);
    syn68k_set_full_sr(core->reg(cpu::m68k::SR));
    cpu_state.vbr = core->reg(cpu::m68k::VBR);
}

static void cpu_state_to_core()
{
    uint16 sr = syn68k_full_sr();
    if(core->reg(cpu::m68k::SR) != sr)
        core->set_reg(cpu::m68k::SR, sr);
    /* After SR: a supervisor-bit change swaps stack pointers, and A7 must
     * end up as cpu_state has it. */
    for(int i = 0; i < 16; i++)
        core->set_reg(cpu::m68k::D0 + i, cpu_state.regs[i].ul.n);
}

namespace {
class SynHost final : public cpu::Host
{
public:
    OpResult host_op(cpu::Core &c, uint32_t opcode, uint32_t op_pc) override
    {
        if(opcode == OP_CALLBACK)
        {
            core_to_cpu_state();
            syn68k_addr_t next = dispatch_callback(op_pc);
            cpu_state_to_core();
            c.set_pc(next);
            /* An interrupt handler is a callback that clears its level. */
            c.set_irq(irq_level());
            return OpResult::Continue;
        }
        if(opcode == OP_EXEC_RETURN)
        {
            c.set_pc(op_pc);  /* stay on the exit cell */
            return OpResult::Stop;
        }
        fprintf(stderr, "syn68k: unexpected host op %04x at %08x\n", opcode, op_pc);
        abort();
    }

    void attention(cpu::Core &c) override
    {
        c.set_irq(irq_level());
    }
};
SynHost syn_host;
}

/* Instruction-at-a-time, while a debugger is attached. Matches syn68k:
 * before each instruction, if getNextBreakpoint(pc) == pc the debugger is
 * entered and returns where to continue. The callback page is never a
 * breakpoint. */
static void step_with_debugger()
{
    syn68k_addr_t pc = core->pc();
    if(!in_callback_page(pc) && syn68k_debugger_callbacks.getNextBreakpoint(pc) == pc)
    {
        core_to_cpu_state();
        syn68k_addr_t next = syn68k_debugger_callbacks.debugger(pc);
        cpu_state_to_core();
        if(next != pc)
        {
            core->set_pc(next);
            return;
        }
    }
    core->step();
}

static void run_until_exit(syn68k_addr_t addr)
{
    bool outermost = emulation_depth == 0;
    syn68k_addr_t oldpc = outermost ? 0 : core->pc();

    cpu_state_to_core();
    core->set_pc(addr);

    ++emulation_depth;
    /* A core can take an interrupt after the exit cell stopped it (UAE
     * services one in the same pass): run until the guest is really back
     * at the exit cell, or the host would read an exception frame as its
     * result. */
    const syn68k_addr_t exit_pc = MAGIC_EXIT_EMULATOR_ADDRESS;
    bool debugging = syn68k_debugger_callbacks.debugger && syn68k_debugger_callbacks.getNextBreakpoint;
    while(core->pc() != exit_pc)
    {
        if(debugging)
            step_with_debugger();
        else
            core->run();
    }
    --emulation_depth;

    core_to_cpu_state();
    if(!outermost)
        core->set_pc(oldpc);
}

/* ---------------------------------------------------------------------- */
/* Running guest code                                                     */
/* ---------------------------------------------------------------------- */

void syn68k_call_emulator(syn68k_addr_t addr)
{
    PUSHADDR(MAGIC_EXIT_EMULATOR_ADDRESS);
    run_until_exit(addr);
}

/* With identity addressing a "code pointer" is just the guest address. */
const uint16 *hash_lookup_code_and_create_if_needed(syn68k_addr_t adr)
{
    return SYN68K_TO_US(adr);
}

void interpret_code(const uint16 *code)
{
    run_until_exit(US_TO_SYN68K(code));
}

syn68k_addr_t syn68k_current_pc(void)
{
    return emulation_depth ? core->pc() : 0;
}

unsigned long destroy_blocks(syn68k_addr_t, uint32)
{
    /* Interpreters only for now: nothing cached. A JIT will flush here. */
    return 0;
}

unsigned long destroy_blocks_with_checksum_mismatch(syn68k_addr_t a, uint32 n)
{
    return destroy_blocks(a, n);
}

void dump_profile(const char *) {}
void m68kaddr(const uint16 *pc) { fprintf(stderr, "%08x\n", US_TO_SYN68K(pc)); }

/* ---------------------------------------------------------------------- */
/* Process contexts                                                       */
/* ---------------------------------------------------------------------- */

namespace {
struct Context
{
    CPUState cpu;
    int depth;
    /* The core's own state follows. */
};
}

size_t syn68k_context_size(void)
{
    return sizeof(Context) + core->context_size();
}

void syn68k_save_context(void *context)
{
    Context *c = (Context *)context;
    memcpy((void *)&c->cpu, (const void *)&cpu_state, sizeof cpu_state);
    c->depth = emulation_depth;
    core->save_context(c + 1);
}

void syn68k_restore_context(const void *context)
{
    const Context *c = (const Context *)context;
    core->restore_context(c + 1);
    /* Everything but the machine's interrupt and trap handler state. */
    memcpy((void *)cpu_state.regs, (const void *)c->cpu.regs, sizeof cpu_state.regs);
    cpu_state.ccnz = c->cpu.ccnz;
    cpu_state.ccn = c->cpu.ccn;
    cpu_state.ccc = c->cpu.ccc;
    cpu_state.ccv = c->cpu.ccv;
    cpu_state.ccx = c->cpu.ccx;
    cpu_state.amode_p = c->cpu.amode_p;
    cpu_state.reversed_amode_p = c->cpu.reversed_amode_p;
    cpu_state.sr = c->cpu.sr;
    cpu_state.vbr = c->cpu.vbr;
    emulation_depth = c->depth;
    /* A level raised or cleared while another context ran. */
    core->set_irq(irq_level());
}

/* ---------------------------------------------------------------------- */
/* Memory                                                                 */
/* ---------------------------------------------------------------------- */

/*
 * Two layouts (syn68k_public.h), fixed at the first allocation:
 *
 *   windows   (Musashi) guest RAM, a pool for syn68k_alloc_low and the
 *             binary's data each get a 1GB window anywhere in the host
 *             address space;
 *   identity  (UAE) guest == host: RAM at host 0, allocations below 4GB,
 *             the binary linked low.
 */
static const uint64 WINDOW = 1ull << 30;
static int layout = -1;  /* -1: not chosen yet, 0: identity, 1: windows */

/* Low allocations: a bump allocator in window 1 (windows), or MAP_32BIT
 * mappings (identity). Freed blocks are kept for reuse by size. */
static uint8_t *pool;
static size_t pool_used;
struct FreeBlock { void *p; size_t size; };
static FreeBlock free_blocks[64];
static int num_free_blocks;

/* Regions the guest may legitimately touch (identity layout). */
struct GuestRegion { uintptr_t lo, hi; };
static GuestRegion regions[64];
static int num_regions = 0;

extern "C" char _etext, _end;  /* the binary's data+bss (and rodata) */

static void set_window(int w, const void *host, uint64 size)
{
    ROMlib_offsets[w] = (uint64)(uintptr_t)host - ((uint64)w << 30);
    ROMlib_sizes[w] = size;
}

static void choose_layout()
{
    if(layout >= 0)
        return;
    layout = engine_name == "uae" ? 0 : 1;
    if(!layout)
        return;  /* identity: the static defaults */

    for(int w = 0; w < OFFSET_TABLE_SIZE; w++)
        set_window(w, (void *)(uintptr_t)(~0ull << 40), 0);  /* nothing yet */
    pool = (uint8_t *)mmap(nullptr, WINDOW, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if(pool == MAP_FAILED)
    {
        fprintf(stderr, "syn68k: cannot reserve the 1GB allocation window (%s)\n", strerror(errno));
        abort();
    }
    set_window(1, pool, WINDOW);
    uintptr_t data = (uintptr_t)&_etext & ~(uintptr_t)0xFFF;
    if((uintptr_t)&_end - data > WINDOW)
    {
        fprintf(stderr, "syn68k: the binary's data is over 1GB\n");
        abort();
    }
    set_window(3, (void *)data, (uintptr_t)&_end - data);
}

static void add_region(uintptr_t lo, size_t len)
{
    for(int i = 0; i < num_regions; i++)
        if(regions[i].lo == 0 && regions[i].hi == 0)
        {
            regions[i] = { lo, lo + len };
            return;
        }
    if(num_regions < (int)(sizeof regions / sizeof regions[0]))
        regions[num_regions++] = { lo, lo + len };
}

static void remove_region(uintptr_t lo)
{
    for(int i = 0; i < num_regions; i++)
        if(regions[i].lo == lo)
            regions[i] = { 0, 0 };
}

int syn68k_is_guest_mapped(uintptr_t addr, size_t len)
{
    if(layout == 1)
    {
        for(int w = 0; w < OFFSET_TABLE_SIZE; w++)
        {
            uint64 base = ROMlib_offsets[w] + ((uint64)w << 30);
            if(addr - base < ROMlib_sizes[w] && addr + len - base <= ROMlib_sizes[w])
                return 1;
        }
        return 0;
    }
    uintptr_t end = addr + len;
    if(addr >= (uintptr_t)&_etext && end <= (uintptr_t)&_end)
        return 1;
    for(int i = 0; i < num_regions; i++)
        if(addr >= regions[i].lo && end <= regions[i].hi && regions[i].hi)
            return 1;
    return 0;
}

void syn68k_bad_host_pointer(const void *p)
{
    fprintf(stderr, "syn68k: host pointer %p is not guest-addressable (outside every window)\n", p);
    abort();
}

void *syn68k_map_guest_ram(size_t size)
{
    choose_layout();
    size = (size + 0xFFFF) & ~(size_t)0xFFFF;
    if(layout == 1)
    {
        if(size + 0x10000 > WINDOW)
        {
            fprintf(stderr, "syn68k: guest RAM over 1GB\n");
            abort();
        }
        /* 64KB past the end too: the end of RAM (MemTop, the boot stack's
         * top) converts back to window 0 rather than to whatever window
         * the host happened to place right after it. */
        void *p = mmap(nullptr, size + 0x10000, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if(p == MAP_FAILED)
        {
            fprintf(stderr, "syn68k: cannot map %zu bytes of guest RAM (%s)\n", size, strerror(errno));
            exit(1);
        }
        set_window(0, p, size + 0x10000);
        return p;
    }
    void *p = mmap((void *)0, size, PROT_READ | PROT_WRITE,
                   MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if(p == MAP_FAILED || p != (void *)0)
    {
        fprintf(stderr,
                "syn68k: cannot map guest RAM at address 0 (%s).\n"
                "  Run: sudo sysctl vm.mmap_min_addr=0\n"
                "  (or use the musashi core: --core musashi)\n",
                strerror(errno));
        exit(1);
    }
    add_region(0, size);
    return p;
}

void *syn68k_alloc_low(size_t size)
{
    choose_layout();
    size = (size + 4095) & ~(size_t)4095;
    for(int i = 0; i < num_free_blocks; i++)
        if(free_blocks[i].size == size)
        {
            void *p = free_blocks[i].p;
            free_blocks[i] = free_blocks[--num_free_blocks];
            if(layout == 0)
                add_region((uintptr_t)p, size);
            return p;
        }
    if(layout == 1)
    {
        if(pool_used + size > WINDOW)
        {
            fprintf(stderr, "syn68k: the 1GB allocation window is full\n");
            abort();
        }
        void *p = pool + pool_used;
        pool_used += size;
        return p;
    }
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if(p == MAP_FAILED)
    {
        fprintf(stderr, "syn68k: low allocation of %zu bytes failed\n", size);
        abort();
    }
    add_region((uintptr_t)p, size);
    return p;
}

void syn68k_free_low(void *p, size_t size)
{
    size = (size + 4095) & ~(size_t)4095;
    if(layout == 0)
        remove_region((uintptr_t)p);
    if(num_free_blocks < (int)(sizeof free_blocks / sizeof free_blocks[0]))
        free_blocks[num_free_blocks++] = { p, size };
    else if(layout == 0)
        munmap(p, size);
}

void syn68k_set_jit(int)
{
    /* No engine runs a JIT yet. */
}

/* ---------------------------------------------------------------------- */
/* Init                                                                   */
/* ---------------------------------------------------------------------- */

void initialize_68k_emulator(void (*)(int), int, uint32 trap_vector_storage[64], uint32)
{
    if((uintptr_t)callback_dummy_address_space >> 32)
        syn68k_bad_host_pointer(callback_dummy_address_space);

    /* The core sees the same windows as SYN68K_TO_US. The Platform's
     * memory and CPU entries follow it, as on every machine. */
    choose_layout();
    if(layout == 1)
    {
        for(int w = 0; w < OFFSET_TABLE_SIZE; w++)
            if(ROMlib_sizes[w])
                guest_memory.map((uint32_t)w << 30, (ROMlib_sizes[w] + 0xFFFF) & ~0xFFFFull,
                                 (uint8_t *)SYN68K_TO_US((uint32_t)w << 30));
    }
    else
        guest_memory.map(0, 1ull << 32, (uint8_t *)0);
    if(layout == 1)
        fprintf(stderr, "[syn68k] %s core, windows: RAM %p (%llu MB), alloc %p, data %p\n", engine_name.c_str(),
                (void *)SYN68K_TO_US(0), (unsigned long long)(ROMlib_sizes[0] >> 20),
                (void *)SYN68K_TO_US(1u << 30), (void *)SYN68K_TO_US(3u << 30));
    else
        fprintf(stderr, "[syn68k] %s core, identity addressing (guest RAM at host 0)\n", engine_name.c_str());
    cpu::platform_install(&g_platform);
    cpu::platform_set_memory(&guest_memory);
    cpu::Config config;
    config.model = 68040;
    config.fpu = false;  /* Executor provides SANE natively */
    core = cpu::create(engine_name, config, guest_memory, syn_host).release();
    if(!core)
    {
        fprintf(stderr, "syn68k: cannot start the %s 68k core\n", engine_name.c_str());
        abort();
    }
    cpu::platform_set_core(core);

    /* Fill the callback page. */
    for(int i = 0; i < MAX_CALLBACKS + CALLBACK_SLOP; i++)
        WRITEUW(US_TO_SYN68K(&callback_dummy_address_space[i]), OP_CALLBACK);
    WRITEUW(MAGIC_EXIT_EMULATOR_ADDRESS, OP_EXEC_RETURN);
    WRITEUW(MAGIC_RTE_ADDRESS, 0x4E73);  /* RTE */

    memset(&cpu_state, 0, sizeof cpu_state);
    for(int i = 63; i >= 0; i--)
    {
        volatile TrapHandlerInfo *thi = &cpu_state.trap_handler_info[i];
        thi->callback_address = callback_install(trap_forwarded, (void *)thi);
        WRITEUL(US_TO_SYN68K(&trap_vector_storage[i]), thi->callback_address);
        thi->func = nullptr;
        thi->arg = nullptr;
    }

    /* Supervisor mode, interrupts enabled, like the Mac OS runs apps. */
    core->reset();
    core->set_reg(cpu::m68k::SR, 0x2000);
    core->set_reg(cpu::m68k::VBR, US_TO_SYN68K(trap_vector_storage));
    core->set_reg(cpu::m68k::CACR, 0);
    core_to_cpu_state();
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);
}

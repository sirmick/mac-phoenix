/*
 * syn68k_uae.cpp - syn68k API implemented on MacPhoenix's UAE core.
 *
 * See syn68k_public.h for the model. In short:
 *   - identity addressing (guest == host, all below 4GB),
 *   - callbacks are EmulOp cells in callback_dummy_address_space,
 *   - exceptions go through real guest vectors (VBR), each pointing at a
 *     forwarding callback, exactly as syn68k's trap.c did,
 *   - CALL_EMULATOR is a nested m68k_execute() that ends when the guest
 *     returns into the exit cell (EMULOP_RETURN, opcode 0x7100).
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

#include <syn68k_public.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

extern bool quit_program;
extern uintptr MEMBaseDiff;

/* ---------------------------------------------------------------------- */
/* Opcodes in the callback page                                           */
/* ---------------------------------------------------------------------- */

static const uint16 OP_EXEC_RETURN = 0x7100;  /* UAE EMULOP_RETURN: leave m68k_execute */
static const uint16 OP_CALLBACK    = 0x7101;  /* EmulOp: dispatch callback by PC      */
static const uint16 OP_RTE         = 0x4E73;

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
uint64 ROMlib_offsets[OFFSET_TABLE_SIZE] = { 0, 0, 0, 0 };
uint64 ROMlib_sizes[OFFSET_TABLE_SIZE] = { 0x100000000ULL, 0, 0, 0 };
DebuggerCallbacks syn68k_debugger_callbacks = { nullptr, nullptr };
int syn68k_track_pc = 0;
int emulation_depth = 0;

static bool use_jit = false;

/* ---------------------------------------------------------------------- */
/* Register sync                                                          */
/* ---------------------------------------------------------------------- */

static void uae_to_cpu_state()
{
    for(int i = 0; i < 16; i++)
        cpu_state.regs[i].ul.n = regs.regs[i];
    MakeSR();
    cpu_state.sr = regs.sr & ~0x1F;
    cpu_state.ccc = GET_CFLG;
    cpu_state.ccv = GET_VFLG;
    cpu_state.ccn = GET_NFLG;
    cpu_state.ccx = GET_XFLG;
    cpu_state.ccnz = !GET_ZFLG;
    cpu_state.vbr = regs.vbr;
}

static void cpu_state_to_uae()
{
    for(int i = 0; i < 16; i++)
        regs.regs[i] = cpu_state.regs[i].ul.n;
    SET_CFLG(cpu_state.ccc != 0);
    SET_VFLG(cpu_state.ccv != 0);
    SET_NFLG(cpu_state.ccn != 0);
    SET_XFLG(cpu_state.ccx != 0);
    SET_ZFLG(cpu_state.ccnz == 0);
    /* Host code may change the interrupt mask or supervisor bits. */
    MakeSR();
    if((regs.sr & ~0x1F) != cpu_state.sr)
    {
        regs.sr = (regs.sr & 0x1F) | (cpu_state.sr & ~0x1F);
        MakeFromSR();
    }
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
            fprintf(stderr, "syn68k-uae: out of callback slots\n");
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

/* EmulOp hook: UAE calls this for 0x71xx (xx != 0), then advances PC by 2. */
static bool facade_emulop(uint16_t opcode, bool /*is_primary*/)
{
    uaecptr pc = m68k_getpc();
    if(opcode != OP_CALLBACK)
    {
        fprintf(stderr, "syn68k-uae: unexpected EmulOp %04x at %08x\n", opcode, pc);
        abort();
    }
    uint32 slot = slot_of(pc);
    if(pc < CALLBACK_STUB_BASE || slot >= num_callback_slots || !callbacks[slot].func)
    {
        fprintf(stderr, "syn68k-uae: jump to dead callback at %08x\n", pc);
        abort();
    }

    uae_to_cpu_state();
    syn68k_addr_t next = callbacks[slot].func(pc, callbacks[slot].arg);
    cpu_state_to_uae();

    /* UAE adds 2 after the EmulOp returns. */
    m68k_setpc(next - 2);
    fill_prefetch_0();
    return true;
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
    if(!thi->func)
    {
        int n = thi - cpu_state.trap_handler_info;
        const char *d = trap_description(n);
        fprintf(stderr, "Unhandled trap %d%s%s%s at pc %08x\n", n,
                d ? " (" : "", d ? d : "", d ? ")" : "", READUL(EM_A7 + 2));
        return MAGIC_RTE_ADDRESS;
    }
    return thi->func(READUL(EM_A7 + 2), thi->arg);
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
static int highest_pending()
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

void interrupt_generate(unsigned priority)
{
    cpu_state.interrupt_pending[priority] = 1;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_CHANGED);
}

void interrupt_note_if_present(void)
{
    if(highest_pending() > 0)
        SPCFLAGS_SET(SPCFLAG_INT);
}

/* g_platform.m68k_poll_interrupts: polled from UAE's tick check. A level
 * that is still pending (masked, or not yet acknowledged) re-raises the
 * flag on every poll until a handler clears it. */
static void facade_poll_interrupts(void)
{
    if(highest_pending() > 0)
        SPCFLAGS_SET(SPCFLAG_INT);
}

/* g_platform.m68k_intlev: level to deliver, or -1. */
static int facade_intlev(void)
{
    return highest_pending();
}

/* syn68k idiom used by host code that polls for interrupts. */
syn68k_addr_t interrupt_process_any_pending(syn68k_addr_t pc)
{
    int level = facade_intlev();
    int mask = (cpu_state.sr >> 8) & 7;
    if(level < 0 || (level <= mask && level != 7))
    {
        settle_interrupt_status();
        return pc;
    }
    uint16 old_sr = cpu_state.sr | (cpu_state.ccc ? 1 : 0) | (cpu_state.ccv ? 2 : 0)
                  | (!cpu_state.ccnz ? 4 : 0) | (cpu_state.ccn ? 8 : 0)
                  | (cpu_state.ccx ? 16 : 0);
    uint32 vector = 24 + level;
    /* 68040 format-0 frame: SR, PC, format/vector word. */
    PUSHUW(vector * 4);
    PUSHUL(pc);
    PUSHUW(old_sr);
    cpu_state.sr = (cpu_state.sr & ~0x0700) | (level << 8) | 0x2000;
    return READUL(cpu_state.vbr + vector * 4);
}

/* ---------------------------------------------------------------------- */
/* Running guest code                                                     */
/* ---------------------------------------------------------------------- */

static bool in_callback_page(uaecptr pc)
{
    uaecptr lo = US_TO_SYN68K(&callback_dummy_address_space[0]);
    uaecptr hi = US_TO_SYN68K(&callback_dummy_address_space[MAX_CALLBACKS + CALLBACK_SLOP]);
    return pc >= lo && pc < hi;
}

/* Instruction-at-a-time loop used while a debugger is attached. Matches
 * syn68k: before each instruction, if getNextBreakpoint(pc) == pc the
 * debugger is entered and returns where to continue. The callback page is
 * never a breakpoint. */
static void execute_with_debugger()
{
    for(;;)
    {
        if(quit_program)
            break;
        uaecptr pc = m68k_getpc();
        bool skip_check = false;
        if(!in_callback_page(pc)
           && syn68k_debugger_callbacks.getNextBreakpoint
           && syn68k_debugger_callbacks.getNextBreakpoint(pc) == pc)
        {
            uae_to_cpu_state();
            uaecptr next = syn68k_debugger_callbacks.debugger(pc);
            cpu_state_to_uae();
            if(next != pc)
            {
                m68k_setpc(next);
                fill_prefetch_0();
                continue;
            }
            skip_check = true;
        }
        (void)skip_check;
        uae_u32 opcode = GET_OPCODE;
        (*cpufunctbl[opcode])(opcode);
        cpu_check_ticks();
        if(SPCFLAGS_TEST(SPCFLAG_ALL_BUT_EXEC_RETURN))
            m68k_do_specialties();
    }
}

static void run_until_exit(syn68k_addr_t addr)
{
    bool outermost = emulation_depth == 0;
    uaecptr oldpc = outermost ? 0 : m68k_getpc();

    cpu_state_to_uae();
    m68k_setpc(addr);
    fill_prefetch_0();

    ++emulation_depth;
    /* UAE services a pending interrupt in the same specialties pass that
     * honours the exit request, so m68k_execute() can return with the guest
     * just diverted into an interrupt handler (PC at the handler, a frame on
     * A7, mask raised). Keep running until the guest is really back at the
     * exit cell; otherwise the host reads the frame as its result. */
    const uaecptr exit_pc = MAGIC_EXIT_EMULATOR_ADDRESS;
    do
    {
        quit_program = false;
        if(syn68k_debugger_callbacks.debugger && syn68k_debugger_callbacks.getNextBreakpoint)
            execute_with_debugger();
        else
            m68k_execute();
    } while(m68k_getpc() != exit_pc);
    quit_program = false;
    --emulation_depth;

    uae_to_cpu_state();
    if(!outermost)
    {
        m68k_setpc(oldpc);
        fill_prefetch_0();
    }
}

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
    return emulation_depth ? m68k_getpc() : 0;
}

unsigned long destroy_blocks(syn68k_addr_t, uint32)
{
    /* Interpreter only for now: nothing cached. The JIT will flush here. */
    return 0;
}

unsigned long destroy_blocks_with_checksum_mismatch(syn68k_addr_t a, uint32 n)
{
    return destroy_blocks(a, n);
}

void dump_profile(const char *) {}
void m68kaddr(const uint16 *pc) { fprintf(stderr, "%08x\n", US_TO_SYN68K(pc)); }

/* ---------------------------------------------------------------------- */
/* Memory                                                                 */
/* ---------------------------------------------------------------------- */

/* Regions the guest may legitimately touch. Small and append-mostly. */
struct GuestRegion { uintptr_t lo, hi; };
static GuestRegion regions[64];
static int num_regions = 0;

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

extern "C" char _etext, _end;  /* the binary's data+bss (and rodata) */

int syn68k_is_guest_mapped(uintptr_t addr, size_t len)
{
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
    fprintf(stderr, "syn68k-uae: host pointer %p is above 4GB and not guest-addressable\n", p);
    abort();
}

void *syn68k_map_guest_ram(size_t size)
{
    size = (size + 0xFFFF) & ~(size_t)0xFFFF;
    void *p = mmap((void *)0, size, PROT_READ | PROT_WRITE,
                   MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if(p == MAP_FAILED || p != (void *)0)
    {
        fprintf(stderr,
                "syn68k-uae: cannot map guest RAM at address 0 (%s).\n"
                "  Run: sudo sysctl vm.mmap_min_addr=0\n",
                strerror(errno));
        exit(1);
    }
    add_region(0, size);
    return p;
}

void *syn68k_alloc_low(size_t size)
{
    size = (size + 4095) & ~(size_t)4095;
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if(p == MAP_FAILED)
    {
        fprintf(stderr, "syn68k-uae: low allocation of %zu bytes failed\n", size);
        abort();
    }
    add_region((uintptr_t)p, size);
    return p;
}

void syn68k_free_low(void *p, size_t size)
{
    remove_region((uintptr_t)p);
    munmap(p, (size + 4095) & ~(size_t)4095);
}

void syn68k_set_jit(int enabled)
{
    use_jit = enabled != 0;
}

/* ---------------------------------------------------------------------- */
/* UAE memory hooks: identity, big-endian                                 */
/* ---------------------------------------------------------------------- */

static uint8_t mem_rb(uint32_t a) { return READUB(a); }
static uint16_t mem_rw(uint32_t a) { return READUW(a); }
static uint32_t mem_rl(uint32_t a) { return READUL(a); }
static void mem_wb(uint32_t a, uint8_t v) { WRITEUB(a, v); }
static void mem_ww(uint32_t a, uint16_t v) { WRITEUW(a, v); }
static void mem_wl(uint32_t a, uint32_t v) { WRITEUL(a, v); }
static uint8_t *mem_m2h(uint32_t a) { return (uint8_t *)(uintptr_t)a; }
static uint32_t mem_h2m(uint8_t *p) { return US_TO_SYN68K(p); }

/* ---------------------------------------------------------------------- */
/* Init                                                                   */
/* ---------------------------------------------------------------------- */

void initialize_68k_emulator(void (*)(int), int, uint32 trap_vector_storage[64], uint32)
{
    extern int CPUType, FPUType;
    CPUType = 4;   /* 68040 */
    FPUType = 0;   /* no FPU: Executor provides SANE natively */

    MEMBaseDiff = 0;
    if((uintptr_t)callback_dummy_address_space >> 32)
        syn68k_bad_host_pointer(callback_dummy_address_space);

    /* Fill the callback page. */
    for(int i = 0; i < MAX_CALLBACKS + CALLBACK_SLOP; i++)
        WRITEUW(US_TO_SYN68K(&callback_dummy_address_space[i]), OP_CALLBACK);
    WRITEUW(MAGIC_EXIT_EMULATOR_ADDRESS, OP_EXEC_RETURN);
    WRITEUW(MAGIC_RTE_ADDRESS, OP_RTE);

    g_platform.mem_read_byte = mem_rb;
    g_platform.mem_read_word = mem_rw;
    g_platform.mem_read_long = mem_rl;
    g_platform.mem_write_byte = mem_wb;
    g_platform.mem_write_word = mem_ww;
    g_platform.mem_write_long = mem_wl;
    g_platform.mem_mac_to_host = mem_m2h;
    g_platform.mem_host_to_mac = mem_h2m;
    g_platform.m68k_emulop_handler = facade_emulop;
    g_platform.m68k_intlev = facade_intlev;
    g_platform.m68k_poll_interrupts = facade_poll_interrupts;
    g_platform.trap_handler = nullptr;   /* real A-line/F-line exceptions */

    init_m68k();
    m68k_reset();

    /* Supervisor mode, interrupts enabled, like the Mac OS runs apps. */
    regs.s = 1;
    regs.m = 0;
    regs.intmask = 0;
    regs.vbr = US_TO_SYN68K(trap_vector_storage);
    MakeSR();

    memset(&cpu_state, 0, sizeof cpu_state);
    for(int i = 63; i >= 0; i--)
    {
        volatile TrapHandlerInfo *thi = &cpu_state.trap_handler_info[i];
        thi->callback_address = callback_install(trap_forwarded, (void *)thi);
        WRITEUL(US_TO_SYN68K(&trap_vector_storage[i]), thi->callback_address);
        thi->func = nullptr;
        thi->arg = nullptr;
    }
    uae_to_cpu_state();
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);
    (void)use_jit;
}

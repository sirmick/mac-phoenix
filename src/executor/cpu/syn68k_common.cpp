/*
 * syn68k_common.cpp - the syn68k API, independent of the 68k core.
 *
 * See syn68k_public.h for the model and syn68k_engine.h for the split. In
 * short:
 *   - identity addressing (guest == host, all below 4GB),
 *   - callbacks are 2-byte cells in callback_dummy_address_space holding an
 *     opcode the engine traps (SYN68K_OP_CALLBACK),
 *   - exceptions go through real guest vectors (VBR), each pointing at a
 *     forwarding callback, exactly as syn68k's trap.c did,
 *   - CALL_EMULATOR runs guest code until it returns into the exit cell
 *     (SYN68K_OP_EXEC_RETURN); the engine does the running.
 */
#include "syn68k_engine.h"

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
uint64 ROMlib_offsets[OFFSET_TABLE_SIZE] = { 0, 0, 0, 0 };
uint64 ROMlib_sizes[OFFSET_TABLE_SIZE] = { 0x100000000ULL, 0, 0, 0 };
DebuggerCallbacks syn68k_debugger_callbacks = { nullptr, nullptr };
int syn68k_track_pc = 0;
int emulation_depth = 0;

static const Syn68kEngine *engine = &syn68k_uae_engine;
static bool engine_started = false;

int syn68k_select_engine(const char *name)
{
    const Syn68kEngine *e = nullptr;
    if(!name || !*name || !strcmp(name, "uae"))
        e = &syn68k_uae_engine;
    else if(!strcmp(name, "musashi"))
        e = &syn68k_musashi_engine;
    if(!e || engine_started)
        return 0;
    engine = e;
    return 1;
}

const char *syn68k_engine_name(void)
{
    return engine->name;
}

/* ---------------------------------------------------------------------- */
/* Status register                                                        */
/* ---------------------------------------------------------------------- */

uint16 syn68k_full_sr(void)
{
    return cpu_state.sr | (cpu_state.ccc ? 1 : 0) | (cpu_state.ccv ? 2 : 0)
         | (!cpu_state.ccnz ? 4 : 0) | (cpu_state.ccn ? 8 : 0)
         | (cpu_state.ccx ? 16 : 0);
}

void syn68k_set_full_sr(uint16 sr)
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

syn68k_addr_t syn68k_dispatch_callback(syn68k_addr_t pc)
{
    uint32 slot = slot_of(pc);
    if(pc < CALLBACK_STUB_BASE || slot >= num_callback_slots || !callbacks[slot].func)
    {
        fprintf(stderr, "syn68k: jump to dead callback at %08x\n", pc);
        abort();
    }
    return callbacks[slot].func(pc, callbacks[slot].arg);
}

bool syn68k_in_callback_page(syn68k_addr_t pc)
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
int syn68k_highest_pending(void)
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
    if(syn68k_highest_pending() > 0)
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
    if(syn68k_highest_pending() > 0)
        engine->note_interrupt();
}

/* syn68k idiom used by host code that polls for interrupts. */
syn68k_addr_t interrupt_process_any_pending(syn68k_addr_t pc)
{
    int level = syn68k_highest_pending();
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
/* Running guest code                                                     */
/* ---------------------------------------------------------------------- */

void syn68k_call_emulator(syn68k_addr_t addr)
{
    PUSHADDR(MAGIC_EXIT_EMULATOR_ADDRESS);
    engine->run_until_exit(addr);
}

/* With identity addressing a "code pointer" is just the guest address. */
const uint16 *hash_lookup_code_and_create_if_needed(syn68k_addr_t adr)
{
    return SYN68K_TO_US(adr);
}

void interpret_code(const uint16 *code)
{
    engine->run_until_exit(US_TO_SYN68K(code));
}

syn68k_addr_t syn68k_current_pc(void)
{
    return emulation_depth ? engine->current_pc() : 0;
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
    /* The engine's own state follows. */
};
}

size_t syn68k_context_size(void)
{
    return sizeof(Context) + engine->context_size();
}

void syn68k_save_context(void *context)
{
    Context *c = (Context *)context;
    memcpy((void *)&c->cpu, (const void *)&cpu_state, sizeof cpu_state);
    c->depth = emulation_depth;
    engine->save_context(c + 1);
}

void syn68k_restore_context(const void *context)
{
    const Context *c = (const Context *)context;
    engine->restore_context(c + 1);
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
    /* A pending interrupt raised while another context ran. */
    interrupt_note_if_present();
}

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
    fprintf(stderr, "syn68k: host pointer %p is above 4GB and not guest-addressable\n", p);
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
                "syn68k: cannot map guest RAM at address 0 (%s).\n"
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
        fprintf(stderr, "syn68k: low allocation of %zu bytes failed\n", size);
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
    engine_started = true;

    /* Fill the callback page. */
    for(int i = 0; i < MAX_CALLBACKS + CALLBACK_SLOP; i++)
        WRITEUW(US_TO_SYN68K(&callback_dummy_address_space[i]), SYN68K_OP_CALLBACK);
    WRITEUW(MAGIC_EXIT_EMULATOR_ADDRESS, SYN68K_OP_EXEC_RETURN);
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
    engine->init(US_TO_SYN68K(trap_vector_storage));
    SET_INTERRUPT_STATUS(INTERRUPT_STATUS_UNCHANGED);
}

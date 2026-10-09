/*
 * syn68k_public.h - syn68k API facade over a 68k core.
 *
 * Executor was written against syn68k. This header keeps that API so the
 * Toolbox code compiles unchanged. syn68k_common.cpp implements it on a
 * cpu::Core (src/cpu/core/cpu_core.h): MacPhoenix's UAE interpreter or
 * Musashi.
 *
 * Addressing is identity: a guest address IS the host address. Guest RAM
 * is mapped at host 0 and everything the guest can see (the binary's
 * globals, the emulator thread's stack, the framebuffer, the callback
 * page) lives below 4GB. Executor's four 1GB translation windows collapse
 * into one (UAE runs DIRECT_ADDRESSING with MEMBaseDiff == 0).
 *
 * The register file lives in `cpu_state` while host code runs (inside a
 * callback or between guest calls) and in the core while guest code runs.
 * The facade syncs them at every host/guest boundary.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(BIGENDIAN) && !defined(LITTLEENDIAN)
# if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define BIGENDIAN
# else
#  define LITTLEENDIAN
# endif
#endif

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

typedef uint32 syn68k_addr_t;
typedef uintptr_t ptr_sized_uint;

#if defined(LITTLEENDIAN)
typedef union {
  struct { uint32 n; } ul;
  struct { int32 n; } sl;
  struct { uint16 n, hi; } uw;
  struct { int16 n, hi; } sw;
  struct { uint8 n, b1, b2, b3; } ub;
  struct { int8 n, b1, b2, b3; } sb;
} M68kReg;
#else
typedef union {
  struct { uint32 n; } ul;
  struct { int32 n; } sl;
  struct { uint16 hi, n; } uw;
  struct { int16 hi, n; } sw;
  struct { uint8 b3, b2, b1, n; } ub;
  struct { int8 b3, b2, b1, n; } sb;
} M68kReg;
#endif

typedef syn68k_addr_t (*callback_handler_t)(syn68k_addr_t, void *);

typedef struct {
  syn68k_addr_t callback_address;
  callback_handler_t func;
  void *arg;
} TrapHandlerInfo;

/* Condition codes: 0 or non-0, NOT 0 or 1. ccnz is non-0 when Z is CLEAR. */
typedef int32 CCRElement;

typedef struct {
  M68kReg regs[16];   /* d0..d7, a0..a7 */
  CCRElement ccnz, ccn, ccc, ccv, ccx;
  syn68k_addr_t amode_p, reversed_amode_p;  /* kept for diagnostics */
  volatile int32 interrupt_status_changed;  /* < 0 when an interrupt is pending */
  volatile uint16 sr;  /* status register without the CCR bits */
  syn68k_addr_t vbr;
  volatile uint8 interrupt_pending[8];
  volatile TrapHandlerInfo trap_handler_info[64];
} CPUState;

extern CPUState cpu_state;

#define EM_DREG(X) (cpu_state.regs[X].ul.n)
#define EM_AREG(X) (cpu_state.regs[8 + (X)].ul.n)
#define EM_D0 EM_DREG (0)
#define EM_D1 EM_DREG (1)
#define EM_D2 EM_DREG (2)
#define EM_D3 EM_DREG (3)
#define EM_D4 EM_DREG (4)
#define EM_D5 EM_DREG (5)
#define EM_D6 EM_DREG (6)
#define EM_D7 EM_DREG (7)
#define EM_A0 EM_AREG (0)
#define EM_A1 EM_AREG (1)
#define EM_A2 EM_AREG (2)
#define EM_A3 EM_AREG (3)
#define EM_A4 EM_AREG (4)
#define EM_A5 EM_AREG (5)
#define EM_A6 EM_AREG (6)
#define EM_A7 EM_AREG (7)
#define EM_FP EM_A6
#define EM_SP EM_A7

/* The callback page: guest-visible 2-byte cells. Cell 0 is the
 * "exit emulator" EmulOp, cell 1 a real RTE, the rest are callbacks. */
extern uint16 callback_dummy_address_space[];
#define CALLBACK_SLOP 16
#define MAGIC_ADDRESS_BASE \
  ((syn68k_addr_t)(uintptr_t)(&callback_dummy_address_space[CALLBACK_SLOP]))
#define MAGIC_EXIT_EMULATOR_ADDRESS (MAGIC_ADDRESS_BASE + 0)
#define MAGIC_RTE_ADDRESS           (MAGIC_ADDRESS_BASE + 2)

typedef struct {
    uint32_t (*debugger)(uint32_t addr);
    uint32_t (*getNextBreakpoint)(uint32_t addr);
} DebuggerCallbacks;
extern DebuggerCallbacks syn68k_debugger_callbacks;

extern int syn68k_track_pc;
syn68k_addr_t syn68k_current_pc(void);
extern int emulation_depth;

/* MacPhoenix: one guest CPU context per Mac process (Process Manager).
 * Saves the register file, condition codes, status register, the
 * interpreter's own state and the nesting depth; interrupt state and the
 * trap handler table belong to the machine and are not part of it. Only
 * valid between instructions, i.e. from host code a trap is running. */
size_t syn68k_context_size(void);
void syn68k_save_context(void *context);
void syn68k_restore_context(const void *context);

#define ADDRESS_BITS 32
#define CLEAN(addr) ((ptr_sized_uint)(addr))
#define ADDRESS_MASK 0xFFFFFFFFU

/* Kept for source compatibility. All offsets are 0 (identity mapping);
 * ROMlib_sizes[0] spans the whole 32-bit space. */
#define OFFSET_TABLE_BITS 2
#define OFFSET_TABLE_SIZE (1 << OFFSET_TABLE_BITS)
extern uint64 ROMlib_offsets[OFFSET_TABLE_SIZE];
extern uint64 ROMlib_sizes[OFFSET_TABLE_SIZE];
#define ROMlib_offset (ROMlib_offsets[0])

/* Called when host code hands the guest a pointer above 4GB. Fatal. */
void syn68k_bad_host_pointer(const void *p) __attribute__((noreturn));

static inline uint16 *SYN68K_TO_US(uint32_t addr)
{
    return (uint16 *)(uintptr_t)addr;
}

static inline uint32_t US_TO_SYN68K_FUN(uint64 addr)
{
    if(__builtin_expect(addr >> 32, 0))
        syn68k_bad_host_pointer((const void *)(uintptr_t)addr);
    return (uint32_t)addr;
}
#define US_TO_SYN68K(addr) (US_TO_SYN68K_FUN((uint64)(uintptr_t)(addr)))

static inline uint16_t *SYN68K_TO_US_CHECK0(uint32_t addr)
{
  return addr ? SYN68K_TO_US(addr) : 0;
}
static inline uint32_t US_TO_SYN68K_CHECK0(const void *addr)
{
  return addr ? US_TO_SYN68K(addr) : 0;
}

#define SWAPUB(val) ((uint8) (val))
#define SWAPSB(val) ((int8)  (val))
static inline uint16_t SWAPUW(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t SWAPUL(uint32_t v) { return __builtin_bswap32(v); }
#define SWAPSW(val) ((int16) SWAPUW (val))
#define SWAPSL(val) ((int32) SWAPUL (val))

#define SWAPUB_IFLE(n) ((uint8) (n))
#define SWAPSB_IFLE(n) ((int8)  (n))
#if defined(BIGENDIAN)
# define SWAPUW_IFLE(n) ((uint16) (n))
# define SWAPSW_IFLE(n) ((int16)  (n))
# define SWAPUL_IFLE(n) ((uint32) (n))
# define SWAPSL_IFLE(n) ((int32)  (n))
#else
# define SWAPUW_IFLE(n) SWAPUW(n)
# define SWAPSW_IFLE(n) SWAPSW(n)
# define SWAPUL_IFLE(n) SWAPUL(n)
# define SWAPSL_IFLE(n) SWAPSL(n)
#endif

static inline uint16_t syn68k_load16(const void *p) { uint16_t v; __builtin_memcpy(&v, p, 2); return v; }
static inline uint32_t syn68k_load32(const void *p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
static inline void syn68k_store16(void *p, uint16_t v) { __builtin_memcpy(p, &v, 2); }
static inline void syn68k_store32(void *p, uint32_t v) { __builtin_memcpy(p, &v, 4); }

#define READUB(addr) (*(const uint8 *) SYN68K_TO_US (CLEAN (addr)))
#define READUW(addr) SWAPUW_IFLE (syn68k_load16 (SYN68K_TO_US (CLEAN (addr))))
#define READUL(addr) SWAPUL_IFLE (syn68k_load32 (SYN68K_TO_US (CLEAN (addr))))
#define READUL_US(addr) SWAPUL_IFLE (syn68k_load32 (addr))
#define READSB(addr) ((int8)  READUB (addr))
#define READSW(addr) ((int16) READUW (addr))
#define READSL(addr) ((int32) READUL (addr))

#define WRITEUB(addr, val) (*(uint8 *) SYN68K_TO_US (CLEAN (addr)) = (val))
#define WRITEUW(addr, val) syn68k_store16 (SYN68K_TO_US (CLEAN (addr)), SWAPUW_IFLE ((uint16)(val)))
#define WRITEUL(addr, val) syn68k_store32 (SYN68K_TO_US (CLEAN (addr)), SWAPUL_IFLE ((uint32)(val)))
#define WRITESB(addr, val)   WRITEUB ((addr), (uint8)  (val))
#define WRITESW(addr, val)   WRITEUW ((addr), (uint16) (val))
#define WRITESL(addr, val)   WRITEUL ((addr), (uint32) (val))

#define POPUB()       (EM_A7 += 2, READUB (EM_A7 - 2))
#define POPUW()       (EM_A7 += 2, READUW (EM_A7 - 2))
#define POPUL()       (EM_A7 += 4, READUL (EM_A7 - 4))
#define POPSB()       ((int8)  POPUB ())
#define POPSW()       ((int16) POPUW ())
#define POPSL()       ((int32) POPUL ())
#define POPADDR()     POPUL()

#define PUSHUB(val)    WRITEUB (EM_A7 -= 2, (val))
#define PUSHUW(val)    WRITEUW (EM_A7 -= 2, (val))
#define PUSHUL(val)    WRITEUL (EM_A7 -= 4, (val))
#define PUSHSB(val)    PUSHUB ((uint8)  (val))
#define PUSHSW(val)    PUSHUW ((uint16) (val))
#define PUSHSL(val)    PUSHUL ((uint32) (val))
#define PUSHADDR(val)  PUSHUL (val)

/* Run guest code at ADDR until it returns (RTS) to the exit cell. */
void syn68k_call_emulator(syn68k_addr_t addr);
#define CALL_EMULATOR(addr) syn68k_call_emulator(addr)

#define SYNCHRONOUS_INTERRUPTS
#define M68K_TIMER_PRIORITY 4
#define M68K_TIMER_VECTOR (24 + M68K_TIMER_PRIORITY)

#define INTERRUPT_STATUS_CHANGED    (-1)
#define INTERRUPT_STATUS_UNCHANGED  0x7FFFFFFF
#define FETCH_INTERRUPT_STATUS() cpu_state.interrupt_status_changed
#define SET_INTERRUPT_STATUS(n) \
  ((void) (cpu_state.interrupt_status_changed = (n)))
#define INTERRUPT_PENDING() (FETCH_INTERRUPT_STATUS () < 0)

/* trap_vector_storage is the guest vector table (VBR). native_p and the
 * other arguments are accepted for compatibility and ignored. */
void initialize_68k_emulator(void (*while_busy)(int), int native_p,
                             uint32 trap_vector_storage[64],
                             uint32 dos_int_flag_addr);

/* Safe to call from signal handlers and other threads. */
void interrupt_generate(unsigned priority);
void interrupt_note_if_present(void);

/* syn68k idiom for taking a pending interrupt from host code:
 *   pc = interrupt_process_any_pending(MAGIC_EXIT_EMULATOR_ADDRESS);
 *   if(pc != MAGIC_EXIT_EMULATOR_ADDRESS)
 *       interpret_code(hash_lookup_code_and_create_if_needed(pc));
 * Pushes an exception frame returning to PC and yields the handler address. */
syn68k_addr_t interrupt_process_any_pending(syn68k_addr_t pc);
void interpret_code(const uint16 *code);
const uint16 *hash_lookup_code_and_create_if_needed(syn68k_addr_t adr);

unsigned long destroy_blocks(syn68k_addr_t low_m68k_address, uint32 num_bytes);
unsigned long destroy_blocks_with_checksum_mismatch(syn68k_addr_t low_m68k_address,
                                                    uint32 num_bytes);
syn68k_addr_t callback_install(callback_handler_t func, void *arbitrary_argument);
void callback_remove(syn68k_addr_t m68k_address);
void *callback_argument(syn68k_addr_t callback_address);
callback_handler_t callback_function(syn68k_addr_t callback_address);
void trap_install_handler(unsigned trap_number, callback_handler_t func,
                          void *arbitrary_argument);
void trap_remove_handler(unsigned trap_number);
void dump_profile(const char *file);
void m68kaddr(const uint16 *pc);

/* MacPhoenix additions. */

/* Map SIZE bytes of guest RAM at guest/host address 0. Requires
 * vm.mmap_min_addr=0. Returns the base (always 0) or aborts. */
void *syn68k_map_guest_ram(size_t size);
/* Allocate host memory the guest can address (below 4GB). */
void *syn68k_alloc_low(size_t size);
void syn68k_free_low(void *p, size_t size);
/* True if [addr, addr+len) lies inside memory the facade knows the guest
 * can touch: guest RAM, low allocations, and the binary's own image. */
int syn68k_is_guest_mapped(uintptr_t addr, size_t len);
/* Turn a JIT on before initialize_68k_emulator() (none yet). */
void syn68k_set_jit(int enabled);
/* Pick the 68k core, "uae" (default) or "musashi", before
 * initialize_68k_emulator(). Returns 0 for an unknown name or once the
 * emulator has started. */
int syn68k_select_engine(const char *name);
const char *syn68k_engine_name(void);

#ifdef __cplusplus
}
#endif

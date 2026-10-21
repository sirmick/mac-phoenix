# UAE CPU Core Quirks and Gotchas

The `uae` core (`--core uae`, the default 68k core) is Basilisk II's UAE
interpreter in `src/cpu/uae_cpu/`, wrapped as a `cpu::Core` by
`src/cpu/core/uae_core.cpp`. It has several non-obvious behaviours that can
cause bugs if you don't understand them. The other 68k core, `mame-68k`,
shares none of this: it reads everything through `GuestMemory`.

Where UAE touches memory:

- **Data** (`get_long`, `put_word`, …, `src/cpu/uae_cpu/memory.h`) goes
  through `g_platform.mem_*`, i.e. the core's `GuestMemory`, which reads and
  writes big-endian bytes.
- **Instruction fetch** goes through the host pointer `regs.pc_p` with the
  `do_get_mem_*` macros below. That needs one host base for the whole guest
  space (`MEMBaseDiff`); `create_uae()` refuses a `GuestMemory` without one.

## Byte-Swapping and HAVE_GET_WORD_UNSWAPPED

### The Problem

This is the most confusing aspect of the UAE CPU core. Understanding this is critical!

**The Setup:**
- Mac ROMs are stored in **big-endian** format (M68K is big-endian)
- x86/x64 hosts are **little-endian**
- Naive approach: "Just byte-swap everything!"
- **Reality: It's more subtle than that**

### How It Actually Works

BasiliskII stores ROM/RAM in **big-endian format** (as if it's on a real Mac), and handles byte-swapping at the CPU/memory interface.

#### The Macro

In `src/common/include/sysdeps.h`, x86 branch:

```c
#define HAVE_GET_WORD_UNSWAPPED
#define do_get_mem_word_unswapped(a) ((uae_u32)*((uae_u16 *)(a)))
```

(`src/cpu/uae_cpu/config.h` `#undef`s it, but `sysdeps.h` includes
`config.h` first and defines it afterwards, so on x86 it is defined. On
other little-endian hosts — arm64, riscv64 — it is not, and `cft_map()` is
the identity.)

When `HAVE_GET_WORD_UNSWAPPED` is defined:
```c
#define GET_OPCODE (do_get_mem_word_unswapped(regs.pc_p))
```

When NOT defined:
```c
#define GET_OPCODE (get_iword(0))  // Calls do_get_mem_word with byte-swapping
```

#### Example: Reading Opcode 0x4efa (JMP)

**Memory contains** (big-endian): `4e fa 00 60`

**With HAVE_GET_WORD_UNSWAPPED** (BasiliskII's approach):
1. `do_get_mem_word_unswapped()` reads raw uint16: `0xfa4e` (little-endian interpretation)
2. This "wrong" value is used to index `cpufunctbl[0xfa4e]`
3. But the table was built with `cft_map()` which byte-swaps indices!
4. So the handler is stored at `cpufunctbl[cft_map(0x4efa)] = cpufunctbl[0xfa4e]`

**Without HAVE_GET_WORD_UNSWAPPED** (non-x86 little-endian hosts):
1. `do_get_mem_word()` reads and byte-swaps: `0x4efa` (correct opcode)
2. Looks up `cpufunctbl[0x4efa]`
3. `cft_map()` is the identity, so the handler is stored at `cpufunctbl[0x4efa]`
4. Also correct, at the cost of a swap per fetch.

What breaks is mixing the two: a fetch path and a table built under
different settings of the macro.

### The cft_map() Function

```c
static __inline__ unsigned int cft_map (unsigned int f)
{
#ifndef HAVE_GET_WORD_UNSWAPPED
    return f;  // No swapping
#else
    return ((f >> 8) & 255) | ((f & 255) << 8);  // Byte-swap
#endif
}
```

**Key insight:** The CPU opcode fetch and the table building MUST match!

- If `GET_OPCODE` returns "unswapped" values → `cft_map` must swap table indices
- If `GET_OPCODE` returns "swapped" values → `cft_map` must not swap table indices

### Why BasiliskII Uses HAVE_GET_WORD_UNSWAPPED

**Performance!**

With `HAVE_GET_WORD_UNSWAPPED`:
```c
// Fast - just read uint16
uint32_t opcode = *((uint16_t *)regs.pc_p);
```

Without it:
```c
// Slower - read and byte-swap every fetch
uint32_t opcode = do_get_mem_word((uint16_t *)regs.pc_p);
// Inside: movzwl, shll, bswapl (3 instructions)
```

The table indices are byte-swapped **once** at startup in `build_cpufunctbl()` (`src/cpu/uae_cpu/newcpu.cpp`), so we avoid byte-swapping on every opcode fetch. The JIT has its own copy of `cft_map()` in `compiler/compemu_support.cpp`.

## WORDS_BIGENDIAN Must Be UNDEFINED

### The Bug

In `src/cpu/uae_cpu/config.h`, we once had:

```c
#define WORDS_BIGENDIAN 0  // WRONG
```

This caused the code to take the big-endian path even on little-endian systems!

### Why?

The code uses `#ifdef` not `#if`:

```c
#ifdef WORDS_BIGENDIAN
    // Big-endian CPU path
    static inline uae_u32 do_get_mem_word(uae_u16 *a) {return *a;}
#else
    // Little-endian CPU path
    static inline uae_u32 do_get_mem_word(uae_u16 *a) {
        uint32 retval;
        __asm__ ("movzwl %w1,%k0\n\tshll $16,%k0\n\tbswapl %k0\n"
                 : "=&r" (retval) : "m" (*a) : "cc");
        return retval;
    }
#endif
```

With `#define WORDS_BIGENDIAN 0`, the symbol IS defined, so `#ifdef WORDS_BIGENDIAN` evaluates to TRUE!

### The Fix

On little-endian systems, **leave WORDS_BIGENDIAN completely undefined**:

```c
/* Byte order - little endian x86 */
/* Don't define WORDS_BIGENDIAN on little-endian systems - leave it undefined */
/*
#ifndef WORDS_BIGENDIAN
#define WORDS_BIGENDIAN 0
#endif
*/
```

On big-endian systems (if we ever support them), define it as 1:

```c
#define WORDS_BIGENDIAN 1
```

## regs.pc vs regs.pc_p

`struct regstruct` (`src/cpu/uae_cpu/newcpu.h`) has both a Mac address and
a host pointer for the PC:

```c
struct regstruct {
    uae_u32  regs[16];  // D0-D7, A0-A7
    uae_u32  pc;        // Mac address: only used without direct addressing
    uae_u8  *pc_p;      // host pointer: the real PC here
    uae_u8  *pc_oldp;
    /* ... */
};
```

MacPhoenix builds UAE with `DIRECT_ADDRESSING=1`, and then **`regs.pc_p` is
the only PC**: `regs.pc` is not kept up to date.

```c
static __inline__ uaecptr m68k_getpc (void)
{
    return get_virtual_address(regs.pc_p);   // host pointer -> Mac address
}

static __inline__ void m68k_setpc (uaecptr newpc)
{
    regs.pc_p = get_real_address(newpc);      // Mac address -> host pointer
}
```

`get_real_address()` / `get_virtual_address()` go through
`g_platform.mem_mac_to_host` / `mem_host_to_mac`. Always use
`m68k_getpc()` / `m68k_setpc()` (or `Core::pc()` / `set_pc()`); never read
`regs.pc`. Code that writes `regs.pc_p` directly must keep it inside the
mapped block.

## Prefetch Buffer (Disabled in Our Build)

The real M68K has a prefetch buffer that reads ahead. UAE can simulate this with `USE_PREFETCH_BUFFER=1`, but BasiliskII disables it for simplicity:

```c
#define USE_PREFETCH_BUFFER 0
```

With prefetch disabled (`src/cpu/uae_cpu/config.h`), instructions are fetched directly from `regs.pc_p` without buffering, and `fill_prefetch_0()` is a no-op.

## Inline Assembly Optimizations

UAE uses inline assembly for byte-swapping on x86 (`sysdeps.h`):

```c
static inline uae_u32 do_get_mem_word(uae_u16 *a) {
    uint32 retval;
    __asm__ ("movzwl %w1,%k0\n\t"
             "shll $16,%k0\n\t"
             "bswapl %k0\n"
             : "=&r" (retval)
             : "m" (*a)
             : "cc");
    return retval;
}
```

**What it does:**
1. `movzwl %w1,%k0` - Load 16-bit word, zero-extend to 32-bit
2. `shll $16,%k0` - Shift left 16 bits (value now in upper 16 bits)
3. `bswapl %k0` - Byte-swap 32-bit value (swaps into lower 16 bits)

**Why the weird approach?** This generates efficient x86 code that both reads and byte-swaps in just 3 instructions.

## Memory Access Constraints

### Alignment

UAE assumes unaligned access is OK on x86:

```c
#if defined(__i386__) || defined(__x86_64__)
    // Intel x86 - can do unaligned access
    static inline uae_u32 do_get_mem_long(uae_u32 *a) {
        uint32 retval;
        __asm__ ("bswap %0" : "=r" (retval) : "0" (*a) : "cc");
        return retval;
    }
#elif defined(CPU_CAN_ACCESS_UNALIGNED)
    // Other CPUs that can do unaligned access
    static inline uae_u32 do_get_mem_long(uae_u32 *a) {
        uint32 x = *a;
        return (x >> 24) | (x >> 8) & 0xff00 | (x << 8) & 0xff0000 | (x << 24);
    }
#else
    // CPUs that cannot do unaligned access - must read byte-by-byte
    static inline uae_u32 do_get_mem_long(uae_u32 *a) {
        uint8 *b = (uint8 *)a;
        return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    }
#endif
```

On x86, we can safely cast any byte pointer to `uint32_t*` and read. Other hosts take the `CPU_CAN_ACCESS_UNALIGNED` or byte-by-byte path.

## CPU Level Selection

UAE supports different M68K CPU models (68000, 68010, 68020, 68030, 68040).
`UaeCore` sets `CPUType` from `cpu::Config::model` (68040 → 4) and
`FPUType` from `Config::fpu`; the model comes from the machine profile
(see `CpuModelConfiguration.md`).

```c
// In build_cpufunctbl():
unsigned int cpu_level = 0;  // Default = 68000
if (CPUType == 4)
    cpu_level = 4;      // 68040 with FPU
else {
    if (FPUType)
        cpu_level = 3;  // 68020 with FPU
    else if (CPUType >= 2)
        cpu_level = 2;  // 68020
    else if (CPUType == 1)
        cpu_level = 1;  // 68010
}
```

The opcode table (`cpufunctbl`) is built from different `op_smalltbl_X_ff` tables depending on CPU level. Higher CPU levels include more instructions.

The Quadra profile is a 68040 with FPU, so it gets level 4. The IIci (68030
+ FPU) gets level 3: UAE has no separate 68030 table, so it runs as a
68020 with FPU. The SE gets level 0.

## Register Naming

UAE uses different names than you might expect:

```c
regs.regs[0..7]   // D0-D7 (data registers)
regs.regs[8..15]  // A0-A7 (address registers)

// Helper macros:
#define m68k_dreg(r,num)  ((r).regs[(num)])       // D0-D7
#define m68k_areg(r,num)  ((r).regs[(num) + 8])   // A0-A7
```

A7 (stack pointer) is at `regs.regs[15]`.

## STOP Instruction Behavior

The STOP instruction (`0x4E72`) **does not terminate execution** - it puts the CPU in a stopped state waiting for an interrupt.

### What STOP Does

1. Sets `regs.stopped = 1`
2. Sets `SPCFLAG_STOP` flag
3. Loads immediate value into Status Register (SR)

### Execution Loop Behavior

In `m68k_do_specialties()` (`src/cpu/uae_cpu/newcpu.cpp`), which
`m68k_do_execute()` calls when a special flag is set:

```c
while (SPCFLAGS_TEST( SPCFLAG_STOP )) {
    if (SPCFLAGS_TEST( SPCFLAG_INT | SPCFLAG_DOINT )){
        SPCFLAGS_CLEAR( SPCFLAG_INT | SPCFLAG_DOINT );
        int intr = host_intlev ();
        if (intr != -1 && (intr > regs.intmask || intr == 7)) {
            Interrupt (intr);
            regs.stopped = 0;
            SPCFLAGS_CLEAR( SPCFLAG_STOP );
        }
    }
}
```

**The CPU spins** until `SPCFLAG_INT` or `SPCFLAG_DOINT` is set and the
host's interrupt level (`host_intlev()`, `UaeCore`'s `intlev` hook) is above
`regs.intmask`, or is 7 (NMI).

### How To Exit STOP

Only an interrupt does: inside that loop nothing else is checked, so
`quit_program` and `request_stop()` wait for the next interrupt too. In
practice the tick thread calls `cpu_trigger_interrupt()` 60 times a second;
`UaeCore::request_attention()` sets `SPCFLAG_INT | SPCFLAG_DOINT`, the
`intlev` hook serves the attention request (`RomHost::attention()` raises
the line to level 1 while `InterruptFlags` is non-zero) and returns the
level, and the CPU takes the interrupt and leaves STOP.

A host that wants a stopped CPU to return must raise the line (or an NMI,
level 7), not just set a flag.

## See Also

- [`../MemoryArchitecture.md`](../MemoryArchitecture.md) — how Mac addresses map to host pointers
- [`CpuBackendApi.md`](CpuBackendApi.md) — `cpu::Core` and the Platform CPU entries
- [`CpuModelConfiguration.md`](CpuModelConfiguration.md) — where the CPU model comes from

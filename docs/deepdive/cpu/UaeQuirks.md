# UAE CPU Core Quirks and Gotchas

The UAE (Unix Amiga Emulator) M68K CPU core has several non-obvious behaviors that can cause bugs if you don't understand them. This document explains the quirks we've encountered.

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

```c
#define HAVE_GET_WORD_UNSWAPPED
#define do_get_mem_word_unswapped(a) ((uae_u32)*((uae_u16 *)(a)))
```

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
4. So the handler is stored at `cpufunctbl[cft_map(0x4efa)] = cpufunctbl[0xfa4e]` ✅

**Without HAVE_GET_WORD_UNSWAPPED** (doesn't work for BasiliskII):
1. `do_get_mem_word()` reads and byte-swaps: `0x4efa` (correct opcode)
2. Looks up `cpufunctbl[0x4efa]`
3. But `cft_map()` is identity function (returns unchanged)
4. Handler is stored at `cpufunctbl[0x4efa]`
5. Seems right? **But it's not!** Because `cft_map()` changes behavior based on `#ifdef HAVE_GET_WORD_UNSWAPPED`!

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

The table indices are byte-swapped **once** at startup in `build_cpufunctbl()`, so we avoid byte-swapping on every opcode fetch!

## WORDS_BIGENDIAN Must Be UNDEFINED

### The Bug

In `src/cpu/uae_cpu/config.h`, we initially had:

```c
#define WORDS_BIGENDIAN 0  // ❌ WRONG!
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

The UAE CPU maintains **two** program counters:

```c
struct regstruct {
    uint32_t regs[16];  // D0-D7, A0-A7
    uint32_t pc;        // Mac address (e.g., 0x0200002a)
    uint8_t *pc_p;      // Host pointer (e.g., 0x7ffff770002a)
    // ... more fields ...
};
```

### When To Use Each

**regs.pc** (Mac address):
- Use when Mac code needs to see the address (for PC-relative addressing, etc.)
- Use in `m68k_getpc()` to return current Mac address
- Updated when PC changes

**regs.pc_p** (host pointer):
- Use for fast opcode fetching (`GET_OPCODE` reads from here)
- Use for fast operand fetching (`get_iword()`, etc.)
- Must stay in sync with `regs.pc`!

### Keeping Them In Sync

```c
void m68k_setpc(uint32_t mac_addr) {
    regs.pc = mac_addr;
    regs.pc_p = Mac2HostAddr(mac_addr);  // Translate to host pointer
}
```

If you only update one, the CPU will use the wrong memory location for instruction fetch!

## Prefetch Buffer (Disabled in Our Build)

The real M68K has a prefetch buffer that reads ahead. UAE can simulate this with `USE_PREFETCH_BUFFER=1`, but BasiliskII disables it for simplicity:

```c
#define USE_PREFETCH_BUFFER 0
```

With prefetch disabled, instructions are fetched directly from `regs.pc_p` without buffering.

## Inline Assembly Optimizations

UAE uses inline assembly for byte-swapping on x86:

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

On x86, we can safely cast any byte pointer to `uint32_t*` and read. On ARM or other CPUs, this might cause a fault!

## CPU Level Selection

UAE supports different M68K CPU models (68000, 68010, 68020, 68030, 68040):

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

For Quadra ROMs, we use CPU level 2 (68020 without FPU) even though real Quadras have 68040. This is because:
1. Most ROM code doesn't use 68040-specific instructions
2. The 68020 instruction set is a good baseline
3. FPU emulation can be added later if needed

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

In `m68k_do_execute()` ([newcpu.cpp:1376-1386](newcpu.cpp#L1376-L1386)):

```c
while (SPCFLAGS_TEST( SPCFLAG_STOP )) {
    if (SPCFLAGS_TEST( SPCFLAG_INT | SPCFLAG_DOINT )){
        int intr = intlev();
        if (intr != -1 && intr > regs.intmask) {
            Interrupt(intr);
            regs.stopped = 0;
            SPCFLAG_CLEAR( SPCFLAG_STOP );
        }
    }
}
```

**The CPU loops forever** waiting for `SPCFLAG_INT` or `SPCFLAG_DOINT` to be set with an interrupt level higher than `regs.intmask`.

### How To Exit STOP

Three options:

1. **Generate an interrupt** - Set `SPCFLAG_INT` and ensure interrupt level > `regs.intmask`
2. **Check regs.stopped** - External code can check `regs.stopped` flag and break out of `m68k_execute()`
3. **Set quit_program** - The outer `m68k_execute()` loop checks this flag

### For Testing: Detecting STOP

To test a minimal ROM that ends with STOP:

```c
// Execute ROM
Start680x0();  // Runs forever!

// Better approach - check if stopped
Init680x0();
m68k_reset();

// Run until STOP
while (!regs.stopped && !quit_program) {
    m68k_do_execute();
}

printf("CPU stopped: %d\n", regs.stopped);
```

### Real-World Usage

In BasiliskII, STOP is used when the Mac OS has nothing to do. The main loop:
1. Calls `m68k_execute()` which runs until STOP
2. Waits for host timer interrupts
3. Triggers M68K interrupt via `TriggerInterrupt()`
4. CPU wakes from STOP and handles the interrupt

## See Also

- [Memory Layout](Memory.md) - How Mac addresses map to host pointers
- [CPU Emulation](CPU.md) - Overall CPU architecture

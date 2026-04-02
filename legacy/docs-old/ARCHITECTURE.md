# Core Architecture and CPU Emulation

## CPU Emulation Overview

The emulator supports two CPU architectures with multiple execution modes:

### 68k Emulation (BasiliskII)

Located in `BasiliskII/src/uae_cpu/` (original) and `uae_cpu_2021/` (updated):

| Mode | Location | Performance | Portability |
|------|----------|-------------|-------------|
| **Interpreter** | `uae_cpu/newcpu.cpp` | Slower | All platforms |
| **JIT Compiler** | `uae_cpu/compiler/` | ~10x faster | x86/x86-64 only |

#### Key CPU Files

```
uae_cpu/
├── newcpu.cpp       # Main interpreter loop
├── newcpu.h         # CPU state definitions
├── m68k.h           # 68k instruction definitions
├── readcpu.cpp      # CPU table generation
├── fpu/             # Floating-point unit emulation
│   ├── fpu_ieee.cpp # IEEE-compliant FPU
│   └── fpu_x86.cpp  # x86-native FPU (faster)
└── compiler/        # JIT compilation (x86 only)
    ├── compemu.cpp  # Main JIT engine
    ├── compemu.h    # JIT interfaces
    └── codegen_x86.cpp # x86 code generation
```

#### CPU State Structure

The emulated CPU state is defined in `newcpu.h`:

```cpp
struct regstruct {
    uint32 regs[16];        // D0-D7, A0-A7
    uint32 pc;              // Program counter
    uint8 *pc_p;            // Host pointer to PC
    uint32 usp, isp, msp;   // Stack pointers
    uint16 sr;              // Status register
    // ... FPU registers, etc.
};
```

### PowerPC Emulation (SheepShaver)

Located in `SheepShaver/src/kpx_cpu/`:

- Uses dynamic binary translation
- Integrates with `sheepshaver_glue.cpp` for emulator callbacks
- PowerPC disassembler in `SheepShaver/src/kpx_cpu/src/cpu/ppc/`

## Memory Architecture

### Address Space Layout

BasiliskII uses a 24-bit or 32-bit address space depending on the emulated Mac model:

```
┌────────────────────┐ 0xFFFFFFFF (32-bit mode)
│   ROM Mirror       │
├────────────────────┤
│   I/O Space        │
├────────────────────┤
│   Video RAM        │
├────────────────────┤
│   Mac RAM          │
├────────────────────┤ 0x00000000
│   Low Memory       │
└────────────────────┘
```

### Memory Access Modes

Configured at build time via `--enable-addressing=`:

| Mode | Description | Performance |
|------|-------------|-------------|
| `real` | Direct memory access | Fastest (requires mmap) |
| `direct` | Offset-based access | Fast |
| `banks` | Bank-switched access | Slowest, most portable |

### Key Memory Files

- `BasiliskII/src/include/cpu_emulation.h` - Memory access macros
- `BasiliskII/src/CrossPlatform/vm_alloc.cpp` - Virtual memory allocation
- `BasiliskII/src/CrossPlatform/sigsegv.cpp` - SIGSEGV handler for memory protection

## ROM Patching System

The emulator cannot include Apple ROMs, so it patches a user-supplied ROM at runtime to redirect certain calls to emulated hardware.

### Patch Flow

```
1. ROM loaded into memory
2. CheckROM() validates ROM type and version
3. PatchROM() applies architecture-specific patches
4. InstallDrivers() patches resource-based drivers
```

### Key Patching Files

| File | Purpose |
|------|---------|
| `rom_patches.cpp` | Main ROM patching logic (~53KB) |
| `rsrc_patches.cpp` | Resource fork patches |
| `emul_op.cpp` | Emulator opcode handlers |
| `emul_op.h` | Opcode definitions (M68K_EMUL_OP_*) |

### Emulator Opcodes

Special 68k opcodes (0x71xx range) trigger emulator callbacks:

```cpp
// From emul_op.h
#define M68K_EMUL_OP_VIDEO_OPEN    0x7100
#define M68K_EMUL_OP_VIDEO_CONTROL 0x7101
#define M68K_EMUL_OP_AUDIO_DISPATCH 0x7110
// ... etc
```

When the 68k CPU encounters these opcodes, `EmulOp()` in `emul_op.cpp` handles them by calling the appropriate emulated hardware.

## Interrupt Handling

### Interrupt Sources

The emulator generates interrupts for:
- **VBL (Vertical Blank)** - ~60Hz video refresh
- **Timer** - Time Manager callbacks
- **Serial** - Incoming data
- **Ethernet** - Network packets
- **ADB** - Keyboard/mouse events

### Interrupt Dispatch

```cpp
// Simplified from main loop
void emulator_tick() {
    // Check for pending interrupts
    if (InterruptFlags) {
        // Trigger 68k interrupt
        TriggerInterrupt();
    }
    // Execute CPU instructions
    m68k_execute();
}
```

### Thread Safety

Interrupts from other threads (audio, network) use:
- `SetInterruptFlag()` - Thread-safe flag setting
- `TriggerInterrupt()` - Signal main emulation thread

## VOSF (Video on SEGV Fault)

An optimization for video that uses memory protection:

1. Video RAM is marked read-only
2. Guest writes trigger SIGSEGV
3. Handler marks dirty pages
4. Only dirty pages are redrawn

Located in `CrossPlatform/video_vosf.h` and enabled with `--enable-vosf`.

## Execution Flow

### Startup Sequence

```
main() → PrefsInit() → SysInit() → InitAll()
                                      ├── RAMBaseHost allocation
                                      ├── ROMBaseHost allocation
                                      ├── Load ROM file
                                      ├── PatchROM()
                                      ├── VideoInit()
                                      ├── AudioInit()
                                      ├── EtherInit()
                                      └── Start CPU emulation
```

### Main Loop (Interpreter)

```cpp
// Simplified from uae_cpu/newcpu.cpp
void m68k_go() {
    for (;;) {
        // Fetch opcode
        opcode = get_iword();
        // Decode and execute
        (*cpufunctbl[opcode])();
        // Handle interrupts
        if (regs.spcflags)
            do_specialties();
    }
}
```

### Shutdown Sequence

```
QuitEmulator() → ExitAll()
                    ├── EtherExit()
                    ├── AudioExit()
                    ├── VideoExit()
                    ├── SysExit()
                    └── PrefsExit()
```

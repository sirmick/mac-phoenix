# Platform Architecture - Testing & Driver Strategy

## Overview

This document describes the macemu-next platform architecture designed for:
- **Runtime driver selection** - Choose implementations at startup via CLI args
- **Easy testing** - Inject custom drivers and test ROMs without recompilation
- **High performance** - Zero overhead via function pointers set at startup
- **Gradual migration** - No changes to existing BasiliskII core code

## Core Design Principles

### 1. Single Main Entry Point
- One executable (`macemu`), configured via command-line arguments
- No conditional compilation for different backends
- All driver implementations compiled into every build

### 2. Platform Drivers (Indirection Layer)
- **Platform drivers** contain function pointer tables
- **Implementation drivers** provide actual functionality
- **Loader functions** wire implementations to platform drivers at startup
- Existing BasiliskII code unchanged - calls same API functions

### 3. Custom Test ROMs
- Small, purpose-built ROMs that test one thing
- Simple calling convention using fixed RAM locations
- Progressive complexity: HALT → MOVE → ADD → branches → exceptions

### 4. Runtime CPU Selection
- Single CPU backends: UAE or Unicorn (with/without JIT, with/without logging)
- Dual CPU backend: UAE + Unicorn validation (always slow, for correctness testing)
- Selected at startup, not compile-time

## Architecture

### Platform Struct

```c
// platform.h

typedef enum {
    CPU_UAE,
    CPU_UNICORN,
    CPU_DUAL
} CPUBackend;

struct Platform {
    // Configuration
    CPUBackend cpu_backend;
    bool jit_enabled;
    bool logging;
    int log_level;

    // Memory
    uint8_t *ram;
    uint8_t *rom;
    uint32_t ram_size;
    uint32_t rom_size;

    // Error handling
    char error_msg[256];
    int error_code;
};

void platform_init(Platform *plat);
void platform_shutdown(Platform *plat);
```

### Platform Driver Pattern

Each subsystem (video, disk, audio, serial, ether, CPU) follows this pattern:

```c
// src/drivers/platform/video_platform.cpp

// Internal ops table
static struct {
    void (*init)(void);
    void (*shutdown)(void);
    void (*refresh)(void);
    void (*set_mode)(int w, int h, int depth);
    uint8_t* (*get_framebuffer)(void);
} video_ops = {NULL};  // NULL = safe no-ops

// BasiliskII API (unchanged from original)
void VideoInit(bool classic) {
    if (video_ops.init) {
        video_ops.init();
    }
}

void VideoExit(void) {
    if (video_ops.shutdown) {
        video_ops.shutdown();
    }
}

void VideoRefresh(void) {
    if (video_ops.refresh) {
        video_ops.refresh();
    }
}

// Loader functions (select implementation)
void video_platform_load_sdl(void) {
    video_ops.init = video_sdl_init;
    video_ops.shutdown = video_sdl_shutdown;
    video_ops.refresh = video_sdl_refresh;
    video_ops.set_mode = video_sdl_set_mode;
    video_ops.get_framebuffer = video_sdl_get_framebuffer;
}

void video_platform_load_null(void) {
    video_ops.init = NULL;
    video_ops.shutdown = NULL;
    video_ops.refresh = NULL;
    video_ops.set_mode = NULL;
    video_ops.get_framebuffer = NULL;
}

void video_platform_load_custom(
    void (*init_fn)(void),
    void (*refresh_fn)(void),
    void (*set_mode_fn)(int, int, int)
) {
    video_ops.init = init_fn;
    video_ops.refresh = refresh_fn;
    video_ops.set_mode = set_mode_fn;
    // Others stay NULL
}
```

### Directory Structure

```
src/
  drivers/
    platform/              # Indirection layer (ops tables + loaders)
      video_platform.cpp   # Video ops table
      disk_platform.cpp    # Disk ops table
      serial_platform.cpp  # Serial ops table
      audio_platform.cpp   # Audio ops table
      ether_platform.cpp   # Ether ops table
      cpu_platform.cpp     # CPU ops table

    video/                 # Video implementations
      video_sdl.cpp        # SDL implementation
      video_null.cpp       # No-op (headless/testing)

    disk/                  # Disk implementations
      disk_posix.cpp       # File-based disk
      disk_null.cpp        # No-op

    audio/                 # Audio implementations
      audio_sdl.cpp        # SDL audio
      audio_null.cpp       # No-op

    serial/                # Serial implementations
      serial_pty.cpp       # PTY-based
      serial_null.cpp      # No-op

    ether/                 # Network implementations
      ether_tap.cpp        # TAP device
      ether_null.cpp       # No-op

    cpu/                   # CPU implementations
      uae/                 # UAE backend
      unicorn/             # Unicorn backend
      dualcpu/             # Dual validation backend
```

## CPU Backends

### Single CPU Modes (Performance)

#### UAE Backend
```bash
./macemu --cpu=uae --rom=quadra.rom
./macemu --cpu=uae-jit --rom=quadra.rom  # With JIT enabled
./macemu --cpu=uae --log=instructions    # With instruction logging
```

Features:
- Mature, well-tested implementation
- JIT compilation support for performance
- Optional instruction-by-instruction logging
- Optional flag optimization

#### Unicorn Backend
```bash
./macemu --cpu=unicorn --rom=quadra.rom
```

Features:
- Modern CPU emulation framework
- Used for validation against UAE
- Target for future performance work

### Dual CPU Mode (Validation)

```bash
./macemu --cpu=dual --rom=test.rom --log=divergence.txt
```

Features:
- Runs UAE and Unicorn in lockstep
- Compares CPU state after every instruction
- Logs divergences with full register dumps
- **Intentionally slow** - designed for correctness testing only
- Use to validate Unicorn matches UAE

Purpose:
- Validate Unicorn implementation against known-good UAE
- Find bugs in Unicorn port
- Regression testing when modifying either backend

## Test ROM Strategy

### Test ROM Calling Convention

Test ROMs use fixed RAM locations to communicate results:

```
RAM Layout:
  0x0000: Test status word
          0 = Running
          1 = Pass
          2 = Fail
  0x0002: Error code (if status == 2)
  0x0004: Scratch space for test data
```

### Progressive Test Complexity

#### 1. test_halt.s - Simplest Test
```m68k
; Tests: Can CPU execute one instruction and halt?
    STOP #$2700

; Expected: 1 instruction executed, CPU halted
```

#### 2. test_move.s - Basic Data Movement
```m68k
    MOVE.W #0, $0000      ; Mark test as running
    MOVE.L #$1234, D0     ; Test immediate to register
    CMP.L #$1234, D0
    BNE.S .fail
.pass:
    MOVE.W #1, $0000      ; Mark test passed
    STOP #$2700
.fail:
    MOVE.W #2, $0000      ; Mark test failed
    MOVE.L D0, $0002      ; Store actual value
    STOP #$2700
```

#### 3. test_add.s - Arithmetic
```m68k
    MOVE.W #0, $0000
    MOVE.L #$1234, D0
    MOVE.L #$5678, D1
    ADD.L D0, D1          ; D1 = $68AC
    CMP.L #$68AC, D1
    BNE.S .fail
.pass:
    MOVE.W #1, $0000
    STOP #$2700
.fail:
    MOVE.W #2, $0000
    MOVE.L D1, $0002
    STOP #$2700
```

#### 4. test_memory.s - Memory Access
Tests MOVE to/from RAM with various addressing modes.

#### 5. test_branch.s - Conditional Branches
Tests BEQ, BNE, BCC, BCS, etc.

#### 6. test_flags.s - CCR Flag Setting
Tests that condition code flags are set correctly (most complex!).

#### 7. test_exceptions.s - Exception Handling
Tests trap instructions and exception vectors.

#### 8. test_emulop.s - BasiliskII EMUL_OP
Tests illegal opcodes 0x71xx used for ROM patching.

### Building Test ROMs

```makefile
# Makefile for test ROMs
AS = vasm
ASFLAGS = -Fbin -m68040

roms/%.bin: roms/%.s
	$(AS) $(ASFLAGS) -o $@ $<

all: \
    roms/test_halt.bin \
    roms/test_move.bin \
    roms/test_add.bin \
    roms/test_memory.bin \
    roms/test_branch.bin \
    roms/test_flags.bin
```

## Test Framework

### Test Runner Structure

```cpp
// test_halt.cpp

#include "platform.h"
#include "test_framework.h"

int main() {
    Platform plat = {0};
    platform_init(&plat);

    // Load minimal configuration
    cpu_platform_load_uae(false);  // UAE, no JIT
    video_platform_load_null();    // Headless
    disk_platform_load_null();
    audio_platform_load_null();
    serial_platform_load_null();
    ether_platform_load_null();

    // Load test ROM
    load_test_rom(&plat, "roms/test_halt.bin");

    // Initialize emulator
    if (!InitAll(NULL)) {
        fprintf(stderr, "FAIL: InitAll failed\n");
        return 1;
    }

    // Execute one instruction
    m68k_reset();
    cpu_execute_n(1);

    // Verify CPU halted
    if (!cpu_is_halted()) {
        fprintf(stderr, "FAIL: CPU did not halt\n");
        return 1;
    }

    printf("PASS: test_halt\n");
    platform_shutdown(&plat);
    return 0;
}
```

### Testing with Custom Drivers

```cpp
// test_video_mode.cpp

static bool set_mode_called = false;
static int captured_width, captured_height, captured_depth;

void test_video_set_mode(int w, int h, int depth) {
    set_mode_called = true;
    captured_width = w;
    captured_height = h;
    captured_depth = depth;
}

int main() {
    Platform plat = {0};
    platform_init(&plat);

    // Load CPU
    cpu_platform_load_uae(false);

    // Inject custom video driver
    video_platform_load_custom(
        NULL,                  // init
        NULL,                  // refresh
        test_video_set_mode    // set_mode - our spy
    );

    // Null everything else
    disk_platform_load_null();
    audio_platform_load_null();
    serial_platform_load_null();
    ether_platform_load_null();

    // Load ROM that calls video mode change
    load_test_rom(&plat, "roms/test_video_mode.bin");

    // Run
    InitAll(NULL);
    cpu_execute_n(100);

    // Verify
    if (!set_mode_called) {
        fprintf(stderr, "FAIL: set_mode not called\n");
        return 1;
    }

    if (captured_width != 640 || captured_height != 480) {
        fprintf(stderr, "FAIL: wrong dimensions %dx%d\n",
                captured_width, captured_height);
        return 1;
    }

    printf("PASS: test_video_mode\n");
    platform_shutdown(&plat);
    return 0;
}
```

### Dual-CPU Validation Test

```cpp
// test_add_dual.cpp

int main() {
    Platform plat = {0};
    platform_init(&plat);

    // Load dual-CPU backend
    cpu_platform_load_dualcpu();

    // Null drivers
    video_platform_load_null();
    disk_platform_load_null();
    audio_platform_load_null();
    serial_platform_load_null();
    ether_platform_load_null();

    // Load test ROM
    load_test_rom(&plat, "roms/test_add.bin");

    // Run with validation
    InitAll(NULL);
    m68k_reset();

    // Execute test
    int instructions = cpu_execute_until_halt();

    // Check for divergences
    DualCPUStats stats;
    dualcpu_get_stats(&stats);

    if (stats.divergences > 0) {
        fprintf(stderr, "FAIL: %lu divergences detected\n",
                stats.divergences);
        fprintf(stderr, "See cpu_validation.log for details\n");
        return 1;
    }

    // Check test result
    uint16_t result = *(uint16_t*)&plat.ram[0x0000];
    if (result != 1) {
        fprintf(stderr, "FAIL: Test ROM reported failure\n");
        return 1;
    }

    printf("PASS: test_add_dual (%d instructions, no divergence)\n",
           instructions);
    platform_shutdown(&plat);
    return 0;
}
```

## Main Entry Point

```cpp
// main.cpp

void print_usage() {
    printf("Usage: macemu [options]\n");
    printf("Options:\n");
    printf("  --cpu=<backend>       CPU backend: uae, uae-jit, unicorn, dual\n");
    printf("  --video=<driver>      Video driver: sdl, null\n");
    printf("  --disk=<driver>       Disk driver: posix, null\n");
    printf("  --audio=<driver>      Audio driver: sdl, null\n");
    printf("  --rom=<file>          ROM file to load\n");
    printf("  --log=<level>         Log level: 0-3\n");
    printf("  --log-file=<file>     Log output file\n");
}

int main(int argc, char **argv) {
    Platform plat = {0};
    platform_init(&plat);

    // Parse command-line arguments
    const char *cpu_backend = get_arg(argc, argv, "--cpu", "uae");
    const char *video_driver = get_arg(argc, argv, "--video", "sdl");
    const char *disk_driver = get_arg(argc, argv, "--disk", "posix");
    const char *audio_driver = get_arg(argc, argv, "--audio", "sdl");
    const char *rom_file = get_arg(argc, argv, "--rom", NULL);

    if (!rom_file) {
        print_usage();
        return 1;
    }

    plat.log_level = get_arg_int(argc, argv, "--log", 0);

    // Load CPU backend
    if (!strcmp(cpu_backend, "uae")) {
        cpu_platform_load_uae(false);
    } else if (!strcmp(cpu_backend, "uae-jit")) {
        cpu_platform_load_uae(true);
    } else if (!strcmp(cpu_backend, "unicorn")) {
        cpu_platform_load_unicorn();
    } else if (!strcmp(cpu_backend, "dual")) {
        cpu_platform_load_dualcpu();
    } else {
        fprintf(stderr, "Unknown CPU backend: %s\n", cpu_backend);
        return 1;
    }

    // Load drivers
    if (!strcmp(video_driver, "sdl")) {
        video_platform_load_sdl();
    } else if (!strcmp(video_driver, "null")) {
        video_platform_load_null();
    }

    if (!strcmp(disk_driver, "posix")) {
        disk_platform_load_posix("/path/to/disk.img");
    } else if (!strcmp(disk_driver, "null")) {
        disk_platform_load_null();
    }

    if (!strcmp(audio_driver, "sdl")) {
        audio_platform_load_sdl();
    } else if (!strcmp(audio_driver, "null")) {
        audio_platform_load_null();
    }

    // Always null for now (not implemented yet)
    serial_platform_load_null();
    ether_platform_load_null();

    // Load ROM
    if (!load_rom(&plat, rom_file)) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_file);
        return 1;
    }

    // Initialize BasiliskII
    if (!InitAll(NULL)) {
        fprintf(stderr, "BasiliskII initialization failed\n");
        return 1;
    }

    // Start emulation (never returns)
    Start680x0();

    // Cleanup (only reached if Start680x0 returns)
    platform_shutdown(&plat);
    return 0;
}
```

## Usage Examples

### Running Full Emulator

```bash
# Standard configuration (UAE + SDL)
./macemu --cpu=uae --video=sdl --audio=sdl --rom=quadra.rom

# With JIT for better performance
./macemu --cpu=uae-jit --video=sdl --rom=quadra.rom

# Unicorn backend
./macemu --cpu=unicorn --video=sdl --rom=quadra.rom

# Headless (for automation)
./macemu --cpu=uae --video=null --audio=null --rom=test.rom
```

### Running Tests

```bash
# Build and run all tests
make test

# Run specific test
./build/tests/test_halt

# Run dual-CPU validation test
./build/tests/test_add_dual

# Run with different CPU backends
./build/tests/test_move --cpu=uae
./build/tests/test_move --cpu=unicorn
./build/tests/test_move --cpu=dual
```

### Debugging Workflow

```bash
# 1. Develop test ROM with UAE (fast, known-good)
./macemu --cpu=uae --video=null --rom=roms/test_new_feature.bin

# 2. Validate with dual-CPU (find Unicorn bugs)
./macemu --cpu=dual --log=3 --log-file=debug.log --rom=roms/test_new_feature.bin

# 3. If divergence found, examine logs
cat debug.log
cat cpu_validation.log

# 4. Fix Unicorn, repeat until no divergence

# 5. Run on Unicorn alone
./macemu --cpu=unicorn --rom=roms/test_new_feature.bin
```

## Performance Characteristics

### Function Pointer Overhead

The platform driver indirection adds one function pointer call per operation:

```c
// Direct call (no platform)
void VideoRefresh(void) {
    // Direct implementation
}

// Platform call (one indirection)
void VideoRefresh(void) {
    if (video_ops.refresh) {
        video_ops.refresh();  // One indirect call
    }
}
```

**Overhead:** ~1-2 CPU cycles per call (negligible for I/O operations)

**Hot path (CPU):** The CPU execute loop benefits from `execute_n()` batch operations:
```c
// Not used in hot path:
for (int i = 0; i < 1000000; i++) {
    cpu_ops.execute_one();  // 1M indirect calls
}

// Used in hot path:
cpu_ops.execute_n(1000000);  // 1 indirect call, loops internally
```

### NULL Check Optimization

Modern compilers optimize NULL checks to be nearly free:
```c
if (video_ops.refresh) {  // Predicted branch (not taken for null drivers)
    video_ops.refresh();
}
```

For null drivers, the branch predictor learns quickly and overhead is minimal.

## Migration Strategy

### Phase 1: Create Platform Structure (No Breaking Changes)
1. Create `src/drivers/platform/` directory
2. Create `platform.h` with struct definitions
3. Create `video_platform.cpp` with ops table
4. Create `video_null.cpp` with no-op implementations
5. Build system compiles new files alongside existing code

### Phase 2: Add First Loader Call
1. In `main.cpp`, call `video_platform_load_null()` during init
2. Existing video code still works (no calls to platform layer yet)
3. Verify build succeeds

### Phase 3: Migrate One Subsystem (Video)
1. Update `VideoInit()`, `VideoRefresh()`, etc. to call through `video_ops`
2. Test with null driver
3. Implement `video_sdl.cpp`
4. Test with SDL driver
5. Verify existing functionality unchanged

### Phase 4: Migrate Remaining Subsystems
1. Repeat Phase 3 for: disk, audio, serial, ether, CPU
2. Each subsystem migrated independently
3. Can mix platform and non-platform subsystems during transition

### Phase 5: Deprecate Old Dummy Drivers
1. Once all subsystems migrated, remove old `src/drivers/dummy/`
2. Update build system
3. Clean up

### Phase 6: Add CLI Arguments
1. Implement argument parsing in `main.cpp`
2. Add runtime driver selection
3. Update documentation

## Benefits Summary

### For Development
- ✅ **Single binary** - no rebuilds to test different configurations
- ✅ **Fast iteration** - change driver, just relink
- ✅ **Easy debugging** - inject logging drivers without code changes

### For Testing
- ✅ **Custom drivers** - inject test spies/mocks inline
- ✅ **Minimal configuration** - null drivers for headless testing
- ✅ **Dual-CPU validation** - verify Unicorn matches UAE
- ✅ **Progressive complexity** - simple ROMs → complex ROMs

### For Users
- ✅ **Runtime configuration** - `--video=sdl` vs `--video=x11`
- ✅ **Performance tuning** - `--cpu=uae-jit` for speed
- ✅ **Debugging** - `--log=3` for detailed tracing

### For CI/CD
- ✅ **One build** - test all driver combinations
- ✅ **Fast tests** - headless with null drivers
- ✅ **Regression suite** - test ROMs validate correctness

## Next Steps

1. **Create platform.h** - Define Platform struct and loader functions
2. **Implement video_platform.cpp** - First platform driver with ops table
3. **Implement video_null.cpp** - No-op video driver
4. **Add loader call in main.cpp** - Wire up first platform driver
5. **Create first test ROM** - `test_halt.s` with assembler setup
6. **Write test_halt.cpp** - First test using platform drivers
7. **Validate** - Ensure existing code still works
8. **Iterate** - Add more drivers, more test ROMs, more tests

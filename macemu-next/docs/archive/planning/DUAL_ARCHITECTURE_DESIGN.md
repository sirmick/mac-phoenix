# macemu-next Dual Architecture Design
## M68K and PowerPC Runtime Selection

---

## Design Philosophy

**Core Principle**: M68K and PPC are fundamentally different architectures that happen to both emulate Macintosh computers. They should be **completely separate** with **zero abstraction** between them.

### Why No Abstraction?

1. **Different ROM structures** - Mac Plus/II ROMs vs PowerMac ROMs
2. **Different boot sequences** - M68K boots to ROM, PPC boots to nanokernel
3. **Different EmulOp conventions** - 0x71xx (M68K) vs 0x18xxxxxx (PPC)
4. **Different driver models** - NuBus (M68K) vs PCI (PPC)
5. **Different function signatures** - Incompatible APIs
6. **Different global state** - CPUType vs PVR, etc.

Attempting to abstract these differences would create brittle, confusing code that's hard to maintain. Instead: **two clean, separate implementations** that share only the runtime selection logic in `main.cpp`.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                    main.cpp                         │
│                                                     │
│  1. Detect architecture (from ROM or CLI)          │
│  2. Initialize M68K or PPC platform                │
│  3. Call architecture-specific code                │
└─────────────────────────────────────────────────────┘
                     │
        ┌────────────┴────────────┐
        │                         │
        ▼                         ▼
┌──────────────┐          ┌──────────────┐
│   M68K Path  │          │   PPC Path   │
├──────────────┤          ├──────────────┤
│ Platform_M68K│          │ Platform_PPC │
│ rom_patches  │          │ rom_patches  │
│ emul_op      │          │ emul_op      │
│ drivers      │          │ drivers      │
│ CPU backends │          │ CPU backends │
└──────────────┘          └──────────────┘
```

**Key Point**: After architecture selection in `main()`, code never crosses between M68K and PPC paths.

---

## File Organization

### Directory Structure

```
macemu-next/src/
│
├── m68k/                              # M68K implementation (BasiliskII)
│   ├── platform_m68k.h                # Platform_M68K struct definition
│   ├── platform_m68k.cpp              # M68K platform initialization
│   │
│   ├── rom_patches_m68k.cpp           # ROM patching for Mac Plus/II/Quadra
│   ├── rom_patches_m68k.h
│   │
│   ├── emul_op_m68k.cpp               # M68K EmulOp handlers (0x71xx)
│   ├── emul_op_m68k.h
│   │
│   ├── video_m68k.cpp                 # NuBus video driver
│   ├── video_m68k.h
│   ├── audio_m68k.cpp                 # Classic Mac audio
│   ├── audio_m68k.h
│   ├── serial_m68k.cpp                # Serial ports (modem/printer)
│   ├── ether_m68k.cpp                 # Ethernet (EtherTalk)
│   │
│   ├── sony_m68k.cpp                  # Floppy disk driver
│   ├── disk_m68k.cpp                  # Hard disk driver
│   ├── cdrom_m68k.cpp                 # CD-ROM driver
│   ├── scsi_m68k.cpp                  # SCSI driver
│   ├── adb_m68k.cpp                   # Apple Desktop Bus
│   │
│   ├── slot_rom_m68k.cpp              # Slot ROM (NuBus cards)
│   ├── rsrc_patches_m68k.cpp          # Resource patches
│   ├── macos_util_m68k.cpp            # Mac OS utilities
│   └── timer_m68k.cpp                 # Timer management
│
├── ppc/                               # PPC implementation (SheepShaver)
│   ├── platform_ppc.h                 # Platform_PPC struct definition
│   ├── platform_ppc.cpp               # PPC platform initialization
│   │
│   ├── rom_patches_ppc.cpp            # ROM patching for PowerMac
│   ├── rom_patches_ppc.h
│   │
│   ├── emul_op_ppc.cpp                # PPC EmulOp handlers (0x18xxxxxx)
│   ├── emul_op_ppc.h
│   │
│   ├── video_ppc.cpp                  # PCI video driver
│   ├── video_ppc.h
│   ├── audio_ppc.cpp                  # PowerMac audio
│   ├── audio_ppc.h
│   ├── serial_ppc.cpp                 # Serial ports
│   ├── ether_ppc.cpp                  # Ethernet
│   │
│   ├── sony_ppc.cpp                   # Floppy disk driver
│   ├── disk_ppc.cpp                   # Hard disk driver
│   ├── cdrom_ppc.cpp                  # CD-ROM driver
│   ├── scsi_ppc.cpp                   # SCSI driver
│   │
│   ├── thunks_ppc.cpp                 # 68K/PPC thunks
│   ├── name_registry_ppc.cpp          # Name Registry (PPC only)
│   ├── xlowmem_ppc.cpp                # Extended low memory
│   ├── macos_util_ppc.cpp             # Mac OS utilities
│   └── timer_ppc.cpp                  # Timer management
│
├── cpu/
│   ├── m68k/                          # M68K CPU backends
│   │   ├── cpu_m68k_uae.c             # UAE interpreter backend
│   │   ├── cpu_m68k_uae.h
│   │   ├── cpu_m68k_unicorn.cpp       # Unicorn backend (M68K)
│   │   ├── cpu_m68k_unicorn.h
│   │   └── uae_cpu/                   # UAE source code
│   │       ├── basilisk_glue.cpp
│   │       ├── newcpu.cpp
│   │       ├── memory.cpp
│   │       └── ...
│   │
│   └── ppc/                           # PPC CPU backends
│       ├── cpu_ppc_kpx.cpp            # KPX interpreter backend
│       ├── cpu_ppc_kpx.h
│       ├── cpu_ppc_unicorn.cpp        # Unicorn backend (PPC)
│       ├── cpu_ppc_unicorn.h
│       └── kpx_cpu/                   # KPX source code (from SheepShaver)
│           ├── sheepshaver_glue.cpp
│           ├── ppc-cpu.cpp
│           ├── ppc-execute.cpp
│           └── ...
│
├── common/                            # Truly architecture-neutral
│   ├── prefs.cpp                      # Preferences (shared format)
│   ├── prefs.h
│   ├── user_strings.cpp               # Error messages
│   ├── user_strings.h
│   └── debug.h                        # Debug macros
│
├── config/                            # WebRTC config (shared)
│   ├── config_manager.cpp
│   └── config_manager.h
│
├── webserver/                         # WebRTC streaming (shared)
│   ├── webserver_main.cpp
│   ├── api_handlers.cpp
│   └── ...
│
└── main.cpp                           # Entry point - architecture detection
```

### Naming Convention

**Rule**: Every architecture-specific file ends with `_m68k` or `_ppc`

- `rom_patches_m68k.cpp` - Clear it's for M68K
- `rom_patches_ppc.cpp` - Clear it's for PPC
- `prefs.cpp` - No suffix = truly shared

---

## Platform API Definitions

### Platform_M68K Structure

```cpp
// src/m68k/platform_m68k.h

#ifndef PLATFORM_M68K_H
#define PLATFORM_M68K_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // ================================================================
    //  IDENTITY
    // ================================================================
    const char *name;                    // "M68K"

    // ================================================================
    //  MEMORY (shared with PPC, but platform owns pointers)
    // ================================================================
    uint8_t *ram;                        // RAM base (host address space)
    uint8_t *rom;                        // ROM base (host address space)
    uint32_t ram_size;                   // RAM size in bytes
    uint32_t rom_size;                   // ROM size in bytes
    uint32_t ram_base_mac;               // RAM base (Mac address space)
    uint32_t rom_base_mac;               // ROM base (Mac address space)

    // ================================================================
    //  M68K CPU CONFIGURATION
    // ================================================================
    int cpu_type;                        // 0=68000, 1=68010, 2=68020, 3=68030, 4=68040
    int fpu_type;                        // 0=none, 1=68881/68882, 2=68040 internal
    bool cpu_is_68060;                   // Distinguish 68040 vs 68060
    bool twenty_four_bit_addressing;     // 24-bit address space?

    // ================================================================
    //  ROM INFORMATION
    // ================================================================
    uint16_t rom_version;                // ROM_VERSION_64K, ROM_VERSION_PLUS, etc.
    uint32_t universal_info;             // ROM offset of UniversalInfo
    uint32_t putscrap_patch;             // Mac address of PutScrap() patch
    uint32_t getscrap_patch;             // Mac address of GetScrap() patch
    uint32_t rom_breakpoint;             // ROM offset for breakpoint (0=disabled)
    bool print_rom_info;                 // Debug flag

    // ================================================================
    //  ROM & SYSTEM OPERATIONS
    // ================================================================
    bool (*rom_check)(void);             // Verify ROM is valid M68K ROM
    bool (*rom_patch)(void);             // Apply BasiliskII ROM patches
    void (*install_drivers)(uint32_t pb); // Install Mac drivers (pb = param block)
    void (*init_low_mem)(void);          // Initialize Mac Low Memory

    // ================================================================
    //  DRIVER INITIALIZATION (M68K-specific signatures)
    // ================================================================
    bool (*video_init)(bool classic);    // classic = Mac Plus/Classic mode
    void (*video_exit)(void);
    void (*video_refresh)(void);

    void (*audio_init)(void);
    void (*audio_exit)(void);

    void (*serial_init)(void);
    void (*serial_exit)(void);

    void (*ether_init)(void);
    void (*ether_exit)(void);

    void (*scsi_init)(void);
    void (*scsi_exit)(void);

    void (*sony_init)(void);             // Floppy
    void (*disk_init)(void);             // Hard disk
    void (*cdrom_init)(void);            // CD-ROM
    void (*adb_init)(void);              // Apple Desktop Bus
    void (*timer_init)(void);

    // ================================================================
    //  CPU BACKEND (UAE or Unicorn)
    // ================================================================
    const char *cpu_backend_name;        // "UAE" or "Unicorn"

    bool (*cpu_init)(void);
    void (*cpu_exit)(void);
    void (*cpu_reset)(void);

    // Execution
    void (*cpu_execute_fast)(void);      // Run until stopped
    int (*cpu_execute_one)(void);        // Execute one instruction

    // Register access
    uint32_t (*cpu_get_pc)(void);
    uint16_t (*cpu_get_sr)(void);
    uint32_t (*cpu_get_dreg)(int n);     // D0-D7
    uint32_t (*cpu_get_areg)(int n);     // A0-A7
    void (*cpu_set_pc)(uint32_t pc);
    void (*cpu_set_dreg)(int n, uint32_t val);
    void (*cpu_set_areg)(int n, uint32_t val);

    // ================================================================
    //  MEMORY ACCESS (backend-independent)
    // ================================================================
    uint8_t  (*mem_read_byte)(uint32_t addr);
    uint16_t (*mem_read_word)(uint32_t addr);
    uint32_t (*mem_read_long)(uint32_t addr);
    void (*mem_write_byte)(uint32_t addr, uint8_t val);
    void (*mem_write_word)(uint32_t addr, uint16_t val);
    void (*mem_write_long)(uint32_t addr, uint32_t val);

    // Address translation
    uint8_t* (*mem_mac_to_host)(uint32_t addr);
    uint32_t (*mem_host_to_mac)(uint8_t *ptr);

    // ================================================================
    //  EMULOP HANDLER (0x71xx illegal instructions)
    // ================================================================
    void (*emulop_handler)(uint16_t opcode);

} Platform_M68K;

// Global instance
extern Platform_M68K g_platform_m68k;

// Platform initialization
void m68k_platform_init(Platform_M68K *p);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_M68K_H
```

### Platform_PPC Structure

```cpp
// src/ppc/platform_ppc.h

#ifndef PLATFORM_PPC_H
#define PLATFORM_PPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // ================================================================
    //  IDENTITY
    // ================================================================
    const char *name;                    // "PowerPC"

    // ================================================================
    //  MEMORY (shared with M68K, but platform owns pointers)
    // ================================================================
    uint8_t *ram;                        // RAM base (host address space)
    uint8_t *rom;                        // ROM base (host address space)
    uint32_t ram_size;                   // RAM size in bytes
    uint32_t rom_size;                   // ROM size in bytes

    // ================================================================
    //  PPC CPU CONFIGURATION
    // ================================================================
    uint32_t pvr;                        // Processor Version Register
    int64_t cpu_clock_speed;             // CPU clock (Hz)
    int64_t bus_clock_speed;             // Bus clock (Hz)
    int64_t timebase_speed;              // Timebase clock (Hz)

    // ================================================================
    //  PPC SYSTEM INFORMATION
    // ================================================================
    uint32_t kernel_data_addr;           // Address of Kernel Data
    uint32_t boot_globs_addr;            // Address of BootGlobs
    void *toc;                           // Table of Contents pointer
    void *r13;                           // r13 register (SheepShaver context)

    // ================================================================
    //  ROM INFORMATION
    // ================================================================
    int rom_type;                        // ROM type (detected by rom_check)

    // ================================================================
    //  ROM & SYSTEM OPERATIONS
    // ================================================================
    bool (*rom_check)(void);             // Verify ROM is valid PowerMac ROM
    bool (*rom_patch)(void);             // Apply SheepShaver ROM patches
    void (*install_drivers)(void);       // Install Mac drivers (NO param!)
    void (*init_low_mem)(void);          // Initialize Mac Low Memory

    // ================================================================
    //  DRIVER INITIALIZATION (PPC-specific signatures)
    // ================================================================
    bool (*video_init)(void);            // No 'classic' parameter!
    void (*video_exit)(void);
    void (*video_refresh)(void);

    void (*audio_init)(void);
    void (*audio_exit)(void);

    void (*serial_init)(void);
    void (*serial_exit)(void);

    void (*ether_init)(void);
    void (*ether_exit)(void);

    void (*scsi_init)(void);
    void (*scsi_exit)(void);

    void (*sony_init)(void);             // Floppy
    void (*disk_init)(void);             // Hard disk
    void (*cdrom_init)(void);            // CD-ROM
    void (*timer_init)(void);

    // ================================================================
    //  CPU BACKEND (KPX or Unicorn)
    // ================================================================
    const char *cpu_backend_name;        // "KPX" or "Unicorn"

    bool (*cpu_init)(void);
    void (*cpu_exit)(void);
    void (*cpu_reset)(void);

    // Execution
    void (*cpu_execute_fast)(void);      // Run until stopped
    int (*cpu_execute_one)(void);        // Execute one instruction

    // Register access (PPC-specific)
    uint32_t (*cpu_get_pc)(void);
    uint32_t (*cpu_get_lr)(void);        // Link Register
    uint32_t (*cpu_get_ctr)(void);       // Count Register
    uint32_t (*cpu_get_gpr)(int n);      // General Purpose Registers (r0-r31)
    double (*cpu_get_fpr)(int n);        // Floating Point Registers (f0-f31)
    void (*cpu_set_pc)(uint32_t pc);
    void (*cpu_set_gpr)(int n, uint32_t val);

    // ================================================================
    //  MEMORY ACCESS (backend-independent)
    // ================================================================
    uint8_t  (*mem_read_byte)(uint32_t addr);
    uint16_t (*mem_read_word)(uint32_t addr);
    uint32_t (*mem_read_long)(uint32_t addr);
    void (*mem_write_byte)(uint32_t addr, uint8_t val);
    void (*mem_write_word)(uint32_t addr, uint16_t val);
    void (*mem_write_long)(uint32_t addr, uint32_t val);

    // ================================================================
    //  EMULOP HANDLER (0x18xxxxxx PowerPC opcodes)
    // ================================================================
    void (*emulop_handler)(uint32_t opcode);  // 32-bit opcodes!

    // ================================================================
    //  PPC-SPECIFIC: 68K EMULATION (for Execute68k)
    // ================================================================
    void (*execute_68k)(uint32_t pc);    // Execute 68K code from PPC

} Platform_PPC;

// Global instance
extern Platform_PPC g_platform_ppc;

// Platform initialization
void ppc_platform_init(Platform_PPC *p);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_PPC_H
```

---

## Main Entry Point

### main.cpp - Runtime Architecture Selection

```cpp
// src/main.cpp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "m68k/platform_m68k.h"
#include "ppc/platform_ppc.h"
#include "common/prefs.h"
#include "webserver/webserver_main.h"

// Shared memory (both architectures use same RAM/ROM)
static uint8_t *RAMBaseHost = NULL;
static uint8_t *ROMBaseHost = NULL;
static uint32_t RAMSize = 0;
static uint32_t ROMSize = 0;

// Architecture type
typedef enum {
    ARCH_UNKNOWN = 0,
    ARCH_M68K = 1,
    ARCH_PPC = 2
} ArchType;

// Detect architecture from ROM file or command line
static ArchType detect_architecture(int argc, char **argv, const char *rom_path) {
    // Check for explicit --arch flag
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--arch") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "m68k") == 0) return ARCH_M68K;
            if (strcmp(argv[i+1], "ppc") == 0) return ARCH_PPC;
        }
    }

    // Check environment variable
    const char *env_arch = getenv("EMULATOR_ARCH");
    if (env_arch) {
        if (strcmp(env_arch, "m68k") == 0) return ARCH_M68K;
        if (strcmp(env_arch, "ppc") == 0) return ARCH_PPC;
    }

    // Auto-detect from ROM size
    if (rom_path && ROMSize > 0) {
        // M68K ROMs: 64KB, 128KB, 256KB, 512KB, 1MB
        // PPC ROMs: 4MB
        if (ROMSize == 4 * 1024 * 1024) {
            return ARCH_PPC;
        } else if (ROMSize <= 1024 * 1024) {
            return ARCH_M68K;
        }
    }

    // Default to M68K
    return ARCH_M68K;
}

// Load ROM file
static bool load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM: %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    ROMSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fread(ROMBaseHost, 1, ROMSize, f) != ROMSize) {
        fprintf(stderr, "Failed to read ROM\n");
        fclose(f);
        return false;
    }

    fclose(f);
    printf("ROM loaded: %d bytes\n", ROMSize);
    return true;
}

int main(int argc, char **argv) {
    printf("=== macemu-next (Universal Binary) ===\n\n");

    // ====== STEP 1: Allocate shared memory ======
    RAMSize = 32 * 1024 * 1024;  // 32MB
    RAMBaseHost = (uint8_t *)mmap(NULL, RAMSize + 0x1000000,  // Extra space for ROM
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (RAMBaseHost == MAP_FAILED) {
        fprintf(stderr, "Failed to allocate RAM\n");
        return 1;
    }

    ROMBaseHost = RAMBaseHost + RAMSize;
    memset(RAMBaseHost, 0, RAMSize);

    printf("Memory allocated:\n");
    printf("  RAM: %p (%d MB)\n", RAMBaseHost, RAMSize / (1024*1024));
    printf("  ROM: %p\n", ROMBaseHost);

    // ====== STEP 2: Load ROM ======
    const char *rom_path = (argc >= 2) ? argv[1] : NULL;
    if (rom_path) {
        if (!load_rom(rom_path)) {
            return 1;
        }
    }

    // ====== STEP 3: Detect architecture ======
    ArchType arch = detect_architecture(argc, argv, rom_path);

    if (arch == ARCH_M68K) {
        printf("\n=== Architecture: M68K (BasiliskII) ===\n");
        return run_m68k(argc, argv, rom_path);
    }
    else if (arch == ARCH_PPC) {
        printf("\n=== Architecture: PowerPC (SheepShaver) ===\n");
        return run_ppc(argc, argv, rom_path);
    }
    else {
        fprintf(stderr, "Unknown architecture\n");
        return 1;
    }
}

// ================================================================
//  M68K EXECUTION PATH
// ================================================================
static int run_m68k(int argc, char **argv, const char *rom_path) {
    // Initialize M68K platform
    m68k_platform_init(&g_platform_m68k);

    // Set memory pointers
    g_platform_m68k.ram = RAMBaseHost;
    g_platform_m68k.rom = ROMBaseHost;
    g_platform_m68k.ram_size = RAMSize;
    g_platform_m68k.rom_size = ROMSize;

    // Check ROM
    if (!g_platform_m68k.rom_check()) {
        fprintf(stderr, "Invalid M68K ROM\n");
        return 1;
    }

    // Select CPU backend
    const char *backend = getenv("CPU_BACKEND");
    if (!backend) backend = "uae";

    if (strcmp(backend, "unicorn") == 0) {
        cpu_m68k_unicorn_install(&g_platform_m68k);
    } else {
        cpu_m68k_uae_install(&g_platform_m68k);
    }

    printf("CPU Backend: %s\n", g_platform_m68k.cpu_backend_name);

    // Initialize CPU
    if (!g_platform_m68k.cpu_init()) {
        fprintf(stderr, "CPU initialization failed\n");
        return 1;
    }

    // Patch ROM
    if (!g_platform_m68k.rom_patch()) {
        fprintf(stderr, "ROM patching failed\n");
        return 1;
    }

    // Initialize drivers
    bool classic = (g_platform_m68k.rom_version <= ROM_VERSION_CLASSIC);
    if (!g_platform_m68k.video_init(classic)) {
        fprintf(stderr, "Video init failed\n");
        return 1;
    }

    g_platform_m68k.audio_init();
    g_platform_m68k.serial_init();
    g_platform_m68k.ether_init();
    g_platform_m68k.sony_init();
    g_platform_m68k.disk_init();
    g_platform_m68k.cdrom_init();
    g_platform_m68k.scsi_init();
    g_platform_m68k.adb_init();
    g_platform_m68k.timer_init();

    // Install drivers (get param block from A0)
    uint32_t pb = g_platform_m68k.cpu_get_areg(0);
    g_platform_m68k.install_drivers(pb);

    // Reset and run
    g_platform_m68k.cpu_reset();
    printf("\n=== Starting M68K Emulation ===\n");
    g_platform_m68k.cpu_execute_fast();

    return 0;
}

// ================================================================
//  PPC EXECUTION PATH
// ================================================================
static int run_ppc(int argc, char **argv, const char *rom_path) {
    // Initialize PPC platform
    ppc_platform_init(&g_platform_ppc);

    // Set memory pointers
    g_platform_ppc.ram = RAMBaseHost;
    g_platform_ppc.rom = ROMBaseHost;
    g_platform_ppc.ram_size = RAMSize;
    g_platform_ppc.rom_size = ROMSize;

    // Check ROM
    if (!g_platform_ppc.rom_check()) {
        fprintf(stderr, "Invalid PowerPC ROM\n");
        return 1;
    }

    // Select CPU backend
    const char *backend = getenv("CPU_BACKEND");
    if (!backend) backend = "kpx";

    if (strcmp(backend, "unicorn") == 0) {
        cpu_ppc_unicorn_install(&g_platform_ppc);
    } else {
        cpu_ppc_kpx_install(&g_platform_ppc);
    }

    printf("CPU Backend: %s\n", g_platform_ppc.cpu_backend_name);

    // Initialize CPU
    if (!g_platform_ppc.cpu_init()) {
        fprintf(stderr, "CPU initialization failed\n");
        return 1;
    }

    // Patch ROM
    if (!g_platform_ppc.rom_patch()) {
        fprintf(stderr, "ROM patching failed\n");
        return 1;
    }

    // Initialize drivers (no parameters!)
    if (!g_platform_ppc.video_init()) {
        fprintf(stderr, "Video init failed\n");
        return 1;
    }

    g_platform_ppc.audio_init();
    g_platform_ppc.serial_init();
    g_platform_ppc.ether_init();
    g_platform_ppc.sony_init();
    g_platform_ppc.disk_init();
    g_platform_ppc.cdrom_init();
    g_platform_ppc.scsi_init();
    g_platform_ppc.timer_init();

    // Install drivers (no param block!)
    g_platform_ppc.install_drivers();

    // Reset and run
    g_platform_ppc.cpu_reset();
    printf("\n=== Starting PPC Emulation ===\n");
    g_platform_ppc.cpu_execute_fast();

    return 0;
}
```

---

## Build System

### meson.build

```meson
project('macemu-next', ['c', 'cpp'],
    version: '2.0.0',
    default_options: ['c_std=c11', 'cpp_std=c++17']
)

# Dependencies
unicorn_dep = dependency('unicorn')
threads_dep = dependency('threads')
m_dep = cc.find_library('m')

# ================================================================
#  M68K Sources
# ================================================================
m68k_sources = files([
    'src/m68k/platform_m68k.cpp',
    'src/m68k/rom_patches_m68k.cpp',
    'src/m68k/emul_op_m68k.cpp',
    'src/m68k/video_m68k.cpp',
    'src/m68k/audio_m68k.cpp',
    'src/m68k/serial_m68k.cpp',
    'src/m68k/ether_m68k.cpp',
    'src/m68k/sony_m68k.cpp',
    'src/m68k/disk_m68k.cpp',
    'src/m68k/cdrom_m68k.cpp',
    'src/m68k/scsi_m68k.cpp',
    'src/m68k/adb_m68k.cpp',
    'src/m68k/timer_m68k.cpp',
    'src/m68k/slot_rom_m68k.cpp',
    'src/m68k/rsrc_patches_m68k.cpp',
    'src/m68k/macos_util_m68k.cpp',
])

# M68K CPU backends
subdir('src/cpu/m68k')
m68k_sources += m68k_cpu_sources

# ================================================================
#  PPC Sources
# ================================================================
ppc_sources = files([
    'src/ppc/platform_ppc.cpp',
    'src/ppc/rom_patches_ppc.cpp',
    'src/ppc/emul_op_ppc.cpp',
    'src/ppc/video_ppc.cpp',
    'src/ppc/audio_ppc.cpp',
    'src/ppc/serial_ppc.cpp',
    'src/ppc/ether_ppc.cpp',
    'src/ppc/sony_ppc.cpp',
    'src/ppc/disk_ppc.cpp',
    'src/ppc/cdrom_ppc.cpp',
    'src/ppc/scsi_ppc.cpp',
    'src/ppc/timer_ppc.cpp',
    'src/ppc/thunks_ppc.cpp',
    'src/ppc/name_registry_ppc.cpp',
    'src/ppc/xlowmem_ppc.cpp',
    'src/ppc/macos_util_ppc.cpp',
])

# PPC CPU backends
subdir('src/cpu/ppc')
ppc_sources += ppc_cpu_sources

# ================================================================
#  Common Sources (architecture-neutral)
# ================================================================
common_sources = files([
    'src/common/prefs.cpp',
    'src/common/user_strings.cpp',
])

# ================================================================
#  Main executable (includes BOTH architectures)
# ================================================================
executable('macemu-next',
    [m68k_sources, ppc_sources, common_sources, 'src/main.cpp'],
    dependencies: [unicorn_dep, threads_dep, m_dep],
    include_directories: [
        include_directories('src'),
        include_directories('src/m68k'),
        include_directories('src/ppc'),
        include_directories('src/common'),
    ]
)
```

---

## Migration Plan

### Phase 1: Create Directory Structure
1. Create `src/m68k/` and `src/ppc/` directories
2. Create `platform_m68k.h` and `platform_ppc.h`

### Phase 2: Copy Existing Code
1. Copy BasiliskII files → `src/m68k/*_m68k.cpp`
2. Copy SheepShaver files → `src/ppc/*_ppc.cpp`

### Phase 3: Implement Platform Initializers
1. Write `m68k_platform_init()` in `platform_m68k.cpp`
2. Write `ppc_platform_init()` in `platform_ppc.cpp`

### Phase 4: Update main.cpp
1. Remove current architecture code
2. Implement `run_m68k()` and `run_ppc()` functions
3. Implement architecture detection

### Phase 5: Update Build System
1. Update `meson.build` to compile both architectures
2. Add CPU backend subdirectories

---

## Key Benefits

✅ **No abstraction overhead** - Each architecture is completely independent
✅ **Clear separation** - File naming makes it obvious what's for what
✅ **Architecture-specific optimizations** - Can optimize M68K and PPC separately
✅ **Easy to maintain** - No confusing shared code with #ifdefs
✅ **Room for wrinkles** - Each platform has its own quirks without affecting the other
✅ **Single binary** - User doesn't need to know which to download
✅ **Runtime selection** - Just load the ROM and go

This design embraces the differences rather than fighting them.

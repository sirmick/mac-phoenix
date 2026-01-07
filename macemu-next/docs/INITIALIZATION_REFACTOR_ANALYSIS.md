# Initialization Refactor Analysis

**Date:** January 7, 2026
**Issue:** Inelegant prefs/CLI/platform initialization logic with bugs and limitations
**Goal:** Design robust, restartable CPU thread architecture supporting M68K and PPC

---

## Current Architecture Problems

### 1. **Prefs System Confusion** 🔴

**Two competing configuration systems:**
- **Legacy `prefs` system** (BasiliskII heritage) - key/value text format
- **New JSON config** (`config::MacemuConfig`) - structured JSON format

**Current flow:**
```cpp
// main.cpp:160-195
// 1. PrefsInit(NULL, argc, argv) - parses CLI args, modifies argc/argv
// 2. Manually scan argc/argv for --config AGAIN (after PrefsInit consumed it)
// 3. Load JSON config from file
// 4. Store JSON config in global: webrtc::g_config
```

**Problems:**
- ❌ PrefsInit() called unconditionally, even in WebRTC mode where it's largely ignored
- ❌ JSON config loaded AFTER prefs, but prefs need config values
- ❌ `PrefsAddInt32("ramsize", RAMSize)` adds to prefs AFTER RAM is already allocated
- ❌ Hardcoded values: `RAMSize = 32MB`, `cpu = 4`, `fpu = true` override config
- ❌ No way to read RAM size from JSON config before allocation

**Specific bug:**
```cpp
// Line 158: Set RAM size BEFORE PrefsInit
RAMSize = 32 * 1024 * 1024;  // Hardcoded!

// Line 251: Add to prefs AFTER the fact
PrefsAddInt32("ramsize", RAMSize);  // Backwards!

// But JSON config has: "ram": 256  (ignored!)
```

### 2. **CLI Argument Handling is Brittle** 🔴

**Multiple passes over argv:**
```cpp
// Pass 1: Line 162 - Check for --config before PrefsInit
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--config") == 0) ...
}

// Pass 2: Line 171 - PrefsInit() consumes args, sets argv[i] = NULL

// Pass 3: Line 174 - Check for --no-webserver AFTER PrefsInit
for (int i = 1; i < argc; i++) {
    if (argv[i] && strcmp(argv[i], "--no-webserver") == 0) ...
}

// Pass 4: Line 220 - Find ROM file (skip NULL entries)
for (int i = 1; i < argc; i++) {
    if (argv[i] != NULL) {
        rom_path = argv[i];
        break;
    }
}
```

**Problems:**
- ❌ Fragile: Relies on PrefsInit() nulling out consumed args
- ❌ Order-dependent: `--config` must be checked before PrefsInit
- ❌ Special case: `--no-webserver` checked after PrefsInit but not in prefs system
- ❌ Inconsistent: Some flags in prefs, some in manual loops
- ❌ No validation: What if `--config` is last arg? (no value)

### 3. **Platform Initialization is Stateful** 🟡

**Current flow:**
```cpp
// main.cpp:152
platform_init();  // Sets NULL drivers

// main.cpp:198-212
if (enable_webserver) {
    g_platform.video_init = video_webrtc_init;  // Replace driver
    g_platform.audio_init = audio_webrtc_init;
} else {
    // Keep NULL drivers from platform_init()
}
```

**Problems:**
- ⚠️ Driver installation happens in `main()`, not in platform code
- ⚠️ No way to change drivers after initialization
- ⚠️ WebRTC config captured in lambda: `[](bool classic) { return video_webrtc_init(classic, webrtc::g_config); }`
- ⚠️ Global dependency: Drivers need `webrtc::g_config` to exist

### 4. **ROM Loading Path Duplicated** 🟡

**Two separate ROM loading paths:**

**Path 1: main.cpp (CLI mode)**
```cpp
// main.cpp:278-309
if (rom_path) {
    int rom_fd = open(rom_path, O_RDONLY);
    ROMSize = lseek(rom_fd, 0, SEEK_END);
    read(rom_fd, ROMBaseHost, ROMSize);
    close(rom_fd);
}
```

**Path 2: emulator_init.cpp (Deferred mode)**
```cpp
// emulator_init.cpp:69-118
bool load_rom_file(const char* rom_path, ...) {
    int rom_fd = open(rom_path, O_RDONLY);
    off_t size = lseek(rom_fd, 0, SEEK_END);
    read(rom_fd, *rom_base_out, size);
    close(rom_fd);
}
```

**Problems:**
- ❌ Code duplication: Same logic in two places
- ❌ Different error handling: main.cpp returns 1, emulator_init returns false
- ❌ main.cpp path doesn't call `load_rom_file()` - why not?

### 5. **CPU Initialization Mixed with Main** 🔴

**Initialization spread across 3 locations:**

1. **main.cpp:311-501** - Inline initialization (CLI mode)
2. **emulator_init.cpp:120-214** - `init_cpu_subsystem()` (Deferred mode)
3. **emulator_init.cpp:312-388** - `init_emulator_from_config()` (Deferred mode)

**Problems:**
- ❌ Massive duplication: CheckROM, CPUType selection, Init680x0, backend install, PatchROM
- ❌ Different code paths: CLI mode doesn't call `init_cpu_subsystem()`
- ❌ Ordering differences: CLI does subsystems AFTER ROM, deferred does BEFORE CPU
- ❌ Not restartable: No way to tear down CPU and reinitialize

### 6. **No M68K/PPC Abstraction** 🔴

**Hardcoded M68K assumptions:**
```cpp
// main.cpp:321-351 - Only M68K CPU type logic
#if EMULATED_68K
    switch (ROMVersion) {
        case ROM_VERSION_64K: CPUType = 0; ...
        case ROM_VERSION_II: CPUType = 2-4; ...
        case ROM_VERSION_32: CPUType = 2-4; ...
    }
#endif

// emulator_init.cpp:334 - Explicit check
if (strcmp(emulator_type, "m68k") != 0) {
    fprintf(stderr, "ERROR: Only m68k supported\n");
    return false;
}
```

**Problems:**
- ❌ No PPC path: Config has `m68k` and `ppc` sections, but no PPC init code
- ❌ CPU backend selection hardcoded: `CPU_BACKEND` env var only controls M68K backends
- ❌ ROM detection M68K-specific: `CheckROM()` only knows Mac ROM versions
- ❌ Can't switch architectures: Would need to restart entire process

### 7. **Global State Tangled** 🔴

**Globals spread across files:**
```cpp
// main.cpp
extern uint8 *RAMBaseHost;   // Defined in basilisk_glue.cpp
extern uint8 *ROMBaseHost;
extern uint32 RAMSize;
extern uint32 ROMSize;
extern int CPUType;
extern int FPUType;

// emulator_init.cpp
bool g_emulator_initialized = false;  // Defined here
static std::mutex g_init_mutex;

// webrtc namespace
namespace webrtc {
    config::MacemuConfig* g_config = nullptr;
    WebRTCServer* g_server = nullptr;
}

// cpu_state namespace
namespace cpu_state {
    std::atomic<bool> g_running(false);
    std::mutex g_mutex;
    std::condition_variable g_cv;
}
```

**Problems:**
- ❌ Scattered: Memory globals in one file, init state in another, CPU state in third
- ❌ Unclear ownership: Who allocates RAMBaseHost? Who frees it?
- ❌ No RAII: Manual mmap/munmap, no cleanup on failure paths
- ❌ Not restartable: Can't reinitialize globals without process restart

### 8. **CPU Thread Not Self-Contained** 🔴

**Current CPU thread (main.cpp:551-597):**
```cpp
std::thread cpu_thread([]() {
    while (webserver::g_running) {
        // Wait for start signal
        std::unique_lock<std::mutex> lock(cpu_state::g_mutex);
        cpu_state::g_cv.wait(lock, ...);

        // Check if initialized
        if (!g_emulator_initialized) { continue; }

        // Execute CPU
        while (cpu_state::g_running) {
            g_platform.cpu_execute_one();
        }
    }
});
```

**Problems:**
- ❌ Not restartable: Once stopped, can't reinitialize CPU without restart
- ❌ No error recovery: If CPU crashes, thread exits permanently
- ❌ No architecture switching: Can't switch M68K ↔ PPC
- ❌ Global dependencies: Accesses `g_emulator_initialized`, `g_platform`, `cpu_state::*`
- ❌ Mixed concerns: Wait logic + execution logic + initialization check

---

## Architectural Vision

### Goal: Restartable CPU Thread for M68K and PPC

**Requirements:**
1. ✅ Support both M68K (BasiliskII) and PPC (SheepShaver) in same binary
2. ✅ Restart CPU without process restart
3. ✅ Switch architectures at runtime (M68K ↔ PPC)
4. ✅ Clean separation: CLI and WebUI use same CPU thread implementation
5. ✅ Unified configuration: Single source of truth (JSON config)
6. ✅ Robust error handling: Graceful failure, easy retry

---

## Proposed Architecture

### 1. **Unified Configuration System**

**Principle: JSON config is the single source of truth**

```cpp
// New: config/emulator_config.h
struct EmulatorConfig {
    // Architecture
    enum Architecture { M68K, PPC };
    Architecture arch;

    // Memory
    uint32_t ram_mb;

    // CPU
    int cpu_type;  // M68K: 0-4 (68000-68040), PPC: 0-3 (603-G4)
    bool fpu;
    const char* cpu_backend;  // "uae", "unicorn", "dualcpu" (M68K only)

    // ROM
    std::string rom_path;  // Full path, resolved from config

    // Platform drivers
    bool enable_webrtc;
    bool enable_video;
    bool enable_audio;
};

// Load from JSON config + CLI overrides
EmulatorConfig load_emulator_config(const char* config_path,
                                     int argc, char** argv);
```

**Flow:**
```cpp
// 1. Load JSON config
auto json_config = config::load_config(config_path);

// 2. Convert to EmulatorConfig + apply CLI overrides
EmulatorConfig emu_config = emulator_config_from_json(json_config, argc, argv);

// 3. Use EmulatorConfig everywhere (no more PrefsInit!)
```

### 2. **CPU Context (Self-Contained)**

**Principle: All CPU state in one struct**

```cpp
// New: core/cpu_context.h
struct CPUContext {
    // Architecture
    EmulatorConfig::Architecture arch;

    // Memory (owned by context)
    std::unique_ptr<uint8_t[]> ram;
    std::unique_ptr<uint8_t[]> rom;
    uint32_t ram_size;
    uint32_t rom_size;

    // CPU state
    int cpu_type;
    int fpu_type;
    bool twenty_four_bit;

    // Platform (backend pointers)
    Platform platform;

    // Execution state
    std::atomic<bool> running;
    std::atomic<bool> initialized;
    std::mutex mutex;
    std::condition_variable cv;

    // Methods
    bool load_rom(const char* rom_path);
    bool init_m68k(const EmulatorConfig& config);
    bool init_ppc(const EmulatorConfig& config);
    void reset();
    void shutdown();

    // Execution (called by CPU thread)
    void execute_loop();
};
```

**Benefits:**
- ✅ RAII: Memory auto-freed on destruction
- ✅ Restartable: `shutdown()` + `init_*()` reuses same context
- ✅ Thread-safe: Mutex protects state changes
- ✅ Architecture-aware: Separate init paths for M68K/PPC

### 3. **CPU Thread Manager**

**Principle: Reusable, robust CPU thread**

```cpp
// New: core/cpu_thread.h
class CPUThread {
public:
    CPUThread(CPUContext* ctx);
    ~CPUThread();

    // Lifecycle
    bool start();  // Spawn thread (if not running)
    void stop();   // Signal thread to stop
    void join();   // Wait for thread to exit

    // State
    bool is_running() const;
    bool is_initialized() const;

    // Commands (thread-safe)
    bool initialize_m68k(const EmulatorConfig& config);
    bool initialize_ppc(const EmulatorConfig& config);
    bool reset();
    bool pause();
    bool resume();

private:
    CPUContext* ctx_;
    std::thread thread_;

    // Thread main loop
    void thread_main();
};
```

**Usage:**
```cpp
// CLI mode
CPUContext ctx;
CPUThread cpu_thread(&ctx);
cpu_thread.initialize_m68k(config);
cpu_thread.start();
// ... run until Ctrl+C ...
cpu_thread.stop();
cpu_thread.join();

// WebUI mode
CPUContext ctx;
CPUThread cpu_thread(&ctx);
cpu_thread.start();  // Starts thread in waiting state
// ... later, when user clicks "Start" ...
cpu_thread.initialize_m68k(config);
// ... user clicks "Stop" ...
cpu_thread.pause();
// ... user changes to PPC ...
cpu_thread.stop();
cpu_thread.initialize_ppc(config);
cpu_thread.start();
```

### 4. **Initialization Flow (Refactored)**

**CLI Mode:**
```cpp
int main(int argc, char** argv) {
    // 1. Parse config (JSON + CLI overrides)
    EmulatorConfig config = load_emulator_config(
        "~/.config/macemu-next/config.json", argc, argv);

    // 2. Create CPU context
    CPUContext cpu_ctx;

    // 3. Initialize architecture
    bool success;
    if (config.arch == EmulatorConfig::M68K) {
        success = cpu_ctx.init_m68k(config);
    } else {
        success = cpu_ctx.init_ppc(config);
    }
    if (!success) { return 1; }

    // 4. Install platform drivers (from config)
    if (config.enable_webrtc) {
        install_webrtc_drivers(&cpu_ctx.platform, &config);
    }

    // 5. Start CPU thread
    CPUThread cpu_thread(&cpu_ctx);
    cpu_thread.start();

    // 6. Start servers (if WebRTC enabled)
    if (config.enable_webrtc) {
        start_webrtc_server(&cpu_ctx, &config);
    }

    // 7. Wait for shutdown
    wait_for_shutdown();

    // 8. Cleanup (RAII handles memory)
    cpu_thread.stop();
    cpu_thread.join();
    return 0;
}
```

**WebUI Mode:**
```cpp
// API Handler: POST /api/emulator/start
Response handle_emulator_start(const Request& req) {
    // Parse request body for architecture choice
    auto j = nlohmann::json::parse(req.body);
    std::string arch = json_utils::get_string(j, "emulator", "m68k");

    // Load config
    auto json_config = config::load_config(ctx_->config_path);
    EmulatorConfig emu_config = emulator_config_from_json(json_config);

    // Resolve ROM path
    if (arch == "m68k") {
        emu_config.rom_path = resolve_rom_path(json_config.m68k.rom,
                                                json_config.web.storage_dir);
    } else {
        emu_config.rom_path = resolve_rom_path(json_config.ppc.rom,
                                                json_config.web.storage_dir);
    }

    // Initialize CPU context
    bool success;
    if (arch == "m68k") {
        success = g_cpu_ctx.init_m68k(emu_config);
    } else {
        success = g_cpu_ctx.init_ppc(emu_config);
    }

    if (!success) {
        return Response::json("{\"error\": \"Initialization failed\"}");
    }

    // Resume CPU thread (already running, just waiting)
    g_cpu_thread.resume();

    return Response::json("{\"success\": true}");
}
```

### 5. **M68K vs PPC Initialization**

**Architecture-specific init:**

```cpp
// core/cpu_context_m68k.cpp
bool CPUContext::init_m68k(const EmulatorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex);

    if (initialized) {
        shutdown();  // Clean up previous state
    }

    arch = EmulatorConfig::M68K;

    // 1. Allocate memory
    ram_size = config.ram_mb * 1024 * 1024;
    ram.reset(new uint8_t[ram_size]);
    rom.reset(new uint8_t[1024 * 1024]);  // Max 1MB ROM

    // 2. Load ROM
    if (!load_rom(config.rom_path.c_str())) {
        return false;
    }

    // 3. Check ROM and determine CPU type
    if (!CheckROM()) {
        return false;
    }

    cpu_type = determine_m68k_cpu_type(ROMVersion, config.cpu_type);
    fpu_type = config.fpu ? 1 : 0;
    twenty_four_bit = (ROMVersion != ROM_VERSION_32);

    // 4. Initialize UAE memory banking
    Init680x0();

    // 5. Install CPU backend
    if (strcmp(config.cpu_backend, "unicorn") == 0) {
        cpu_unicorn_install(&platform);
    } else if (strcmp(config.cpu_backend, "dualcpu") == 0) {
        cpu_dualcpu_install(&platform);
    } else {
        cpu_uae_install(&platform);
    }

    // 6. Configure CPU
    if (platform.cpu_set_type) {
        platform.cpu_set_type(cpu_type, fpu_type);
    }

    // 7. Patch ROM
    if (!PatchROM()) {
        return false;
    }

    // 8. Initialize CPU
    if (!platform.cpu_init()) {
        return false;
    }

    // 9. Initialize Mac subsystems
    if (!init_mac_subsystems()) {
        return false;
    }

    // 10. Reset CPU
    platform.cpu_reset();

    initialized = true;
    return true;
}

// core/cpu_context_ppc.cpp
bool CPUContext::init_ppc(const EmulatorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex);

    if (initialized) {
        shutdown();
    }

    arch = EmulatorConfig::PPC;

    // 1. Allocate memory (PPC-specific layout)
    ram_size = config.ram_mb * 1024 * 1024;
    ram.reset(new uint8_t[ram_size]);
    rom.reset(new uint8_t[4 * 1024 * 1024]);  // Max 4MB ROM for NewWorld

    // 2. Load ROM (PPC ROM format)
    if (!load_rom(config.rom_path.c_str())) {
        return false;
    }

    // 3. Check ROM (PPC-specific)
    if (!CheckROM_PPC()) {
        return false;
    }

    // 4. Determine PPC CPU type (603, 604, G3, G4)
    cpu_type = determine_ppc_cpu_type(config.cpu_type);

    // 5. Install PPC backend (would need Unicorn PPC or QEMU)
    // TODO: Implement PPC backend
    cpu_ppc_install(&platform);

    // 6. Initialize SheepShaver subsystems
    if (!init_sheepshaver_subsystems()) {
        return false;
    }

    // 7. Reset CPU
    platform.cpu_reset();

    initialized = true;
    return true;
}
```

---

## Migration Plan

### Phase 1: Extract Configuration (Week 1)

**Goal:** Unify prefs and JSON config into `EmulatorConfig`

**Tasks:**
1. Create `config/emulator_config.h/cpp`
2. Implement `load_emulator_config()` - reads JSON + parses CLI
3. Replace hardcoded `RAMSize = 32MB` with config value
4. Remove redundant `PrefsInit()` call (or make it load from EmulatorConfig)
5. Test: CLI mode with `--config` still works

**Files:**
- NEW: `src/config/emulator_config.h`
- NEW: `src/config/emulator_config.cpp`
- MODIFY: `src/main.cpp` (remove prefs logic)

### Phase 2: Create CPU Context (Week 2)

**Goal:** Encapsulate all CPU state in `CPUContext`

**Tasks:**
1. Create `core/cpu_context.h/cpp`
2. Move memory allocation into context (RAII with unique_ptr)
3. Move `init_cpu_subsystem()` logic into `CPUContext::init_m68k()`
4. Add `CPUContext::shutdown()` for cleanup
5. Test: CLI mode using CPUContext

**Files:**
- NEW: `src/core/cpu_context.h`
- NEW: `src/core/cpu_context_m68k.cpp`
- MODIFY: `src/main.cpp` (use CPUContext)
- MODIFY: `src/core/emulator_init.cpp` (use CPUContext)

### Phase 3: Extract CPU Thread (Week 3)

**Goal:** Self-contained, restartable CPU thread

**Tasks:**
1. Create `core/cpu_thread.h/cpp`
2. Implement `CPUThread` class with start/stop/join
3. Move CPU loop from main.cpp into `CPUThread::thread_main()`
4. Add commands: `initialize_m68k()`, `reset()`, `pause()`, `resume()`
5. Test: CLI and WebUI modes with CPUThread

**Files:**
- NEW: `src/core/cpu_thread.h`
- NEW: `src/core/cpu_thread.cpp`
- MODIFY: `src/main.cpp` (use CPUThread)
- MODIFY: `src/webserver/api_handlers.cpp` (use CPUThread)

### Phase 4: Add PPC Support Stubs (Week 4)

**Goal:** Architecture abstraction complete

**Tasks:**
1. Create `core/cpu_context_ppc.cpp` with stub implementation
2. Add `EmulatorConfig::Architecture` enum
3. Add PPC ROM checking stub (`CheckROM_PPC()`)
4. Add PPC backend stub (`cpu_ppc_install()`)
5. Test: Can select PPC in config (fails gracefully with "not implemented")

**Files:**
- NEW: `src/core/cpu_context_ppc.cpp`
- NEW: `src/cpu/cpu_ppc_stub.cpp`
- MODIFY: `src/config/emulator_config.h` (add Architecture enum)

### Phase 5: Implement PPC Backend (Future)

**Goal:** Full PPC support via Unicorn

**Tasks:**
1. Implement Unicorn PPC backend (similar to Unicorn M68K)
2. Implement SheepShaver subsystem initialization
3. Add PPC ROM patching
4. Test with Mac OS 9 ROM

**Timeline:** 4-6 weeks

---

## Benefits of Refactor

### Immediate Benefits
✅ **Restartable CPU:** Stop/start without process restart
✅ **Unified config:** One source of truth (JSON config)
✅ **Clean CLI:** Single-pass argument parsing
✅ **Better errors:** Clear initialization failure messages
✅ **RAII memory:** Auto-cleanup, no leaks

### Long-term Benefits
✅ **M68K + PPC:** Switch architectures at runtime
✅ **Testable:** CPUContext can be unit tested
✅ **Maintainable:** Clear separation of concerns
✅ **CLI = WebUI:** Same code path for both modes
✅ **Future-proof:** Easy to add ARM backend later

---

## Summary of Issues

| Issue | Severity | Impact | Fix Phase |
|-------|----------|--------|-----------|
| Prefs vs JSON confusion | 🔴 Critical | Config values ignored | Phase 1 |
| Hardcoded RAM size | 🔴 Critical | Can't configure RAM | Phase 1 |
| Brittle CLI parsing | 🔴 Critical | Fragile, error-prone | Phase 1 |
| ROM loading duplicated | 🟡 Medium | Code duplication | Phase 2 |
| CPU init duplicated | 🔴 Critical | Maintenance burden | Phase 2 |
| Global state tangled | 🔴 Critical | Not restartable | Phase 2 |
| CPU thread not self-contained | 🔴 Critical | Can't restart | Phase 3 |
| No M68K/PPC abstraction | 🟡 Medium | Can't add PPC | Phase 4 |

---

**Next Steps:** Review this analysis, discuss architecture, then proceed with Phase 1 implementation.

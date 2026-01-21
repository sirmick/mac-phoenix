# Next Session Plan: Deferred CPU Initialization

**Goal:** Enable users to configure ROM in web UI and click "Start" to load ROM and initialize CPU without restarting the process.

**Priority:** High - Required for production usability

---

## Problem Statement

**Current workflow (awkward):**
1. Start: `./build/macemu-next` (no ROM)
2. Configure ROM in web UI
3. **Restart:** `./build/macemu-next "/path/to/rom.ROM"`
4. Click "Start" in web UI

**Desired workflow:**
1. Start: `./build/macemu-next` (no ROM)
2. Configure ROM in web UI
3. Click "Start" → **automatically loads ROM and initializes CPU**
4. Mac boots and streams video/audio

---

## Architecture Design

### Overview

Extract CPU initialization into callable functions that can be invoked on-demand from `/api/emulator/start` endpoint.

### Key Constraints

1. **Must not break command-line mode** - `./build/macemu-next <rom>` must still work
2. **Support both m68k and PPC** - Architecture-agnostic design
3. **Thread-safe** - Initialization from API handler thread
4. **One-time init** - Can't reinitialize without full restart (for now)

### Proposed Changes

#### 1. Create Initialization Functions

**File:** `src/core/emulator_init.h/cpp`

```cpp
// Global state flag
extern bool g_emulator_initialized;

// Load ROM file into memory
// Returns: true on success, false on error
bool load_rom_file(const char* rom_path,
                   uint8_t** rom_base_out,
                   uint32_t* rom_size_out);

// Initialize CPU subsystem (m68k or PPC)
// Must be called after ROM is loaded
// Returns: true on success, false on error
bool init_cpu_subsystem(const char* cpu_backend);

// Full emulator initialization (ROM + CPU + devices)
// Can be called from main() at startup OR from API handler
// Returns: true on success, false on error
bool init_emulator_from_config(const char* emulator_type,
                                const char* storage_dir,
                                const char* rom_filename);
```

#### 2. Refactor main.cpp

**Before:**
```cpp
// main.cpp (current - monolithic init at startup)
int main(int argc, char** argv) {
    // ... setup ...

    if (rom_path) {
        // Load ROM
        int rom_fd = open(rom_path, O_RDONLY);
        read(rom_fd, ROMBaseHost, ROMSize);

        // Init CPU
        if (!Init680x0()) { ... }
        cpu_*_install(&g_platform);
        g_platform.cpu_init();

        // Patch ROM
        if (!PatchROM()) { ... }

        // Create CPU thread
        std::thread cpu_thread(...);
    }

    // Start servers
    WebRTCServer webrtc_server;
    HTTPServer http_server;
}
```

**After:**
```cpp
// main.cpp (refactored - deferred init support)
int main(int argc, char** argv) {
    // ... setup ...

    if (rom_path) {
        // Immediate initialization (command-line mode)
        if (!init_emulator_from_config("m68k", storage_dir, rom_path)) {
            fprintf(stderr, "Failed to initialize emulator\n");
            return 1;
        }
    } else {
        // Deferred initialization (webserver mode)
        printf("Webserver mode - ROM will be loaded when user clicks Start\n");
    }

    // Start servers (regardless of ROM state)
    WebRTCServer webrtc_server;
    HTTPServer http_server;
}
```

#### 3. Update API Handler

**File:** `src/webserver/api_handlers.cpp`

```cpp
Response APIRouter::handle_emulator_start(const Request& req) {
    // Check if already initialized
    if (g_emulator_initialized) {
        // Already running - just wake up CPU
        ctx_->cpu_running->store(true);
        ctx_->cpu_cv->notify_one();
        return Response::json("{\"success\": true, \"message\": \"CPU resumed\"}");
    }

    // Not initialized - need to load ROM first

    // Load config to get ROM path
    auto config = config::load_config(ctx_->prefs_path);

    std::string emulator_type = config.web.emulator;  // "m68k" or "ppc"
    std::string rom_filename;

    if (emulator_type == "m68k") {
        rom_filename = config.m68k.rom;
    } else if (emulator_type == "ppc") {
        rom_filename = config.ppc.rom;
    }

    if (rom_filename.empty()) {
        return Response::json(
            "{\"success\": false, \"error\": \"No ROM configured\"}"
        );
    }

    // Initialize emulator with ROM
    std::string storage_dir = config.web.storage_dir;

    fprintf(stderr, "[API] Initializing emulator: %s with ROM: %s\n",
            emulator_type.c_str(), rom_filename.c_str());

    if (!init_emulator_from_config(emulator_type.c_str(),
                                    storage_dir.c_str(),
                                    rom_filename.c_str())) {
        return Response::json(
            "{\"success\": false, \"error\": \"Failed to load ROM and initialize CPU\"}"
        );
    }

    fprintf(stderr, "[API] Emulator initialized successfully\n");

    // Start CPU
    ctx_->cpu_running->store(true);
    ctx_->cpu_cv->notify_one();

    return Response::json("{\"success\": true, \"message\": \"CPU started\"}");
}
```

---

## Implementation Steps

### Phase 1: Extract ROM Loading (2-3 hours)

**Files to modify:**
- `src/core/emulator_init.cpp` (new)
- `src/core/emulator_init.h` (new)
- `src/main.cpp`

**Tasks:**
1. Create `load_rom_file()` function
   - Open ROM file
   - Allocate/check ROMBaseHost memory
   - Read ROM into memory
   - Set ROMSize global
   - Return success/failure

2. Test command-line mode still works
   ```bash
   ./build/macemu-next ~/quadra.rom
   ```

### Phase 2: Extract CPU Initialization (3-4 hours)

**Files to modify:**
- `src/core/emulator_init.cpp`
- `src/cpu/cpu_init.cpp` (new - extract from main.cpp)

**Tasks:**
1. Create `init_cpu_subsystem()` function
   - CheckROM()
   - Set CPU type based on ROM version
   - Init680x0() for m68k
   - Install CPU backend (UAE/Unicorn/DualCPU)
   - Configure CPU type and FPU
   - Initialize XPRAM
   - PatchROM()
   - Create CPU thread with cv/mutex
   - Set g_platform.cpu_init pointer

2. Handle global state carefully
   - CPU thread, mutex, cv must be initialized
   - Populate APIContext pointers

3. Test command-line mode still works

### Phase 3: Integrate with API Handler (1-2 hours)

**Files to modify:**
- `src/webserver/api_handlers.cpp`
- `src/core/emulator_init.h`

**Tasks:**
1. Add `g_emulator_initialized` check
2. Call `init_emulator_from_config()` if not initialized
3. Build ROM path from config
4. Handle errors gracefully
5. Test webserver mode:
   ```bash
   ./build/macemu-next  # No ROM
   # Configure ROM in web UI
   # Click "Start" → should load ROM and boot Mac
   ```

### Phase 4: Add PPC Support (2 hours)

**Tasks:**
1. Add PPC-specific initialization path
2. Handle different ROM check for PPC
3. Install PPC CPU backend
4. Test with SheepShaver ROM

---

## Testing Plan

### Test Cases

1. **Command-line mode (m68k)**
   ```bash
   ./build/macemu-next ~/quadra.rom
   ```
   - ROM loads immediately
   - CPU initializes
   - Mac boots
   - WebRTC streams video/audio

2. **Webserver mode (m68k)**
   ```bash
   ./build/macemu-next  # No ROM
   ```
   - No ROM loaded initially
   - Configure ROM in web UI
   - Click "Start"
   - ROM loads dynamically
   - CPU initializes
   - Mac boots
   - WebRTC streams video/audio

3. **Error handling**
   - Click "Start" with no ROM configured → Error message
   - Click "Start" with invalid ROM path → Error message
   - Click "Start" with corrupted ROM → Error message

4. **Multiple starts**
   - Click "Start" when already running → CPU resumes (doesn't reinit)

5. **PPC mode** (future)
   - Same tests with `emulator: "ppc"` in config

---

## Potential Issues & Solutions

### Issue 1: Global State Initialization Order

**Problem:** Many globals (RAMBaseHost, ROMBaseHost, g_platform, etc.) are initialized in main() and used throughout CPU init.

**Solution:**
- Keep RAM allocation in main() (needed for both modes)
- Keep ROMBaseHost allocation in main() (pre-allocate max ROM size)
- Only populate ROMBaseHost on-demand
- Set ROMSize = 0 initially, update on ROM load

### Issue 2: Thread Safety

**Problem:** API handler runs in HTTP server thread, CPU init expects to run in main thread.

**Solution:**
- Use mutexes around initialization
- Make init function idempotent (check `g_emulator_initialized` flag)
- Document that init is one-time only

### Issue 3: CPU Thread Lifecycle

**Problem:** CPU thread expects to be created once and run forever.

**Solution:**
- Don't change lifecycle - still create once
- In webserver mode, thread waits on cv until ROM loaded
- API handler signals cv after successful init

### Issue 4: Video/Audio Driver Installation

**Problem:** Drivers installed in main() before ROM loaded.

**Solution:**
- Drivers can be installed early (they're just function pointers)
- Encoder threads can be created early (they wait for data)
- No change needed - current architecture supports this

---

## Code Structure

```
src/
├── core/
│   ├── emulator_init.h           # NEW: Init function declarations
│   ├── emulator_init.cpp         # NEW: Init function implementations
│   └── rom_loader.cpp            # NEW: ROM loading utilities
├── cpu/
│   ├── cpu_init.h                # NEW: CPU-specific init
│   └── cpu_init.cpp              # NEW: Extracted from main.cpp
├── main.cpp                      # MODIFIED: Call init functions
└── webserver/
    └── api_handlers.cpp          # MODIFIED: Call init on /start
```

---

## Success Criteria

1. **Command-line mode works** - No regression
2. **Webserver mode works** - Can load ROM via web UI
3. **No race conditions** - Thread-safe initialization
4. **Error handling** - Clear messages on failure
5. **Code is maintainable** - Well-structured, documented
6. **Both m68k and PPC** - Architecture-agnostic

---

## Alternative Approaches Considered

### Alternative 1: Process Restart

**Approach:** `/api/emulator/start` calls `exec()` to restart process with ROM arg

**Pros:**
- Simplest implementation
- No refactoring needed
- Clean state

**Cons:**
- Loses WebRTC connections
- Awkward user experience (page reload needed)
- Not truly "dynamic"

**Decision:** Rejected - poor UX

### Alternative 2: Hot-Swappable CPU

**Approach:** Stop CPU thread, unload ROM, load new ROM, restart CPU thread

**Pros:**
- Can change ROMs at runtime
- Ultimate flexibility

**Cons:**
- Much more complex
- Need to tear down/rebuild all state
- Out of scope for now

**Decision:** Deferred - implement one-time init first

### Alternative 3: Dual-Process Architecture

**Approach:** Separate emulator and web server processes (like web-streaming)

**Pros:**
- Server can restart emulator independently
- Cleaner separation

**Cons:**
- Defeats purpose of in-process design
- Need IPC (added complexity)

**Decision:** Rejected - in-process is a key design goal

---

## References

- Current implementation: [src/main.cpp](../src/main.cpp)
- Config system: [src/config/config_manager.cpp](../src/config/config_manager.cpp)
- API handlers: [src/webserver/api_handlers.cpp](../src/webserver/api_handlers.cpp)
- Platform layer: [src/common/include/platform.h](../src/common/include/platform.h)

---

## Estimated Timeline

| Phase | Time | Priority |
|-------|------|----------|
| 1. Extract ROM loading | 2-3 hours | High |
| 2. Extract CPU init | 3-4 hours | High |
| 3. API integration | 1-2 hours | High |
| 4. PPC support | 2 hours | Medium |
| **Total** | **8-11 hours** | - |

---

## Notes for Implementation

- Start with m68k only - add PPC later
- Keep changes minimal - don't refactor unnecessarily
- Test after each phase
- Document global state dependencies
- Add comments explaining initialization order
- Use `g_emulator_initialized` flag to prevent double-init
- Consider adding `/api/emulator/reload` for future ROM swapping

---

**Status:** Ready to implement
**Assigned to:** Next session
**Depends on:** WebRTC integration (✅ complete)
**Blocks:** Production deployment, user testing

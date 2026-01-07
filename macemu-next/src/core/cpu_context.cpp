/*
 *  cpu_context.cpp - CPU execution context implementation
 */

#include "cpu_context.h"
#include "emulator_init.h"
#include "main.h"
#include "prefs.h"
#include "rom_patches.h"
#include "cpu_emulation.h"
#include "newcpu.h"
#include "memory.h"
#include "xpram.h"
#include "timer.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "scsi.h"
#include "serial.h"
#include "ether.h"
#include "clip.h"
#include "adb.h"
#include "audio.h"
#include "video.h"
#include "extfs.h"
#include "user_strings.h"
#include "timer_interrupt.h"  // From ../drivers/platform/

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <thread>
#include <chrono>

#define DEBUG 1
#include "debug.h"

// Forward declarations for external variables
// These will eventually be removed when we fully migrate to CPUContext
extern uint8_t *RAMBaseHost;
extern uint8_t *ROMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMSize;
extern int CPUType;
extern int FPUType;
extern bool CPUIs68060;
extern bool TwentyFourBitAddressing;
extern uint16 ROMVersion;

#if DIRECT_ADDRESSING
extern uintptr MEMBaseDiff;
extern uint32 RAMBaseMac;
extern uint32 ROMBaseMac;
#endif

// CPU backend install functions
extern "C" {
void cpu_uae_install(Platform* platform);
void cpu_unicorn_install(Platform* platform);
void cpu_dualcpu_install(Platform* platform);
}

// ========================================
// Constructor / Destructor
// ========================================

CPUContext::CPUContext()
    : architecture_(config::Architecture::M68K)
    , ram_(nullptr)
    , rom_(nullptr)
    , ram_size_(0)
    , rom_size_(0)
    , cpu_type_(0)
    , fpu_type_(0)
    , twenty_four_bit_(false)
    , state_(CPUState::UNINITIALIZED)
{
    // Initialize platform with null drivers
    // (actual drivers installed by caller before init_m68k)
    memset(&platform_, 0, sizeof(platform_));
}

CPUContext::~CPUContext() {
    shutdown();
}

// ========================================
// Memory Management
// ========================================

bool CPUContext::load_rom(const char* rom_path) {
    if (!rom_path || !rom_path[0]) {
        fprintf(stderr, "[CPUContext] ERROR: No ROM path specified\n");
        return false;
    }

    fprintf(stderr, "[CPUContext] Loading ROM from: %s\n", rom_path);

    int rom_fd = open(rom_path, O_RDONLY);
    if (rom_fd < 0) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to open ROM file: %s\n", rom_path);
        return false;
    }

    // Get ROM size
    off_t size = lseek(rom_fd, 0, SEEK_END);
    if (size < 0) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to get ROM size\n");
        close(rom_fd);
        return false;
    }

    fprintf(stderr, "[CPUContext] ROM size: %ld bytes (%ld KB)\n",
            (long)size, (long)size / 1024);

    // Validate ROM size
    if (size != 64*1024 && size != 128*1024 && size != 256*1024 &&
        size != 512*1024 && size != 1024*1024) {
        fprintf(stderr, "[CPUContext] ERROR: Invalid ROM size (must be 64/128/256/512/1024 KB)\n");
        close(rom_fd);
        return false;
    }

    // Read ROM into memory
    lseek(rom_fd, 0, SEEK_SET);
    ssize_t bytes_read = read(rom_fd, rom_.get(), size);
    close(rom_fd);

    if (bytes_read != size) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to read ROM file (read %ld bytes, expected %ld)\n",
                (long)bytes_read, (long)size);
        return false;
    }

    rom_size_ = (uint32_t)size;
    fprintf(stderr, "[CPUContext] ROM loaded successfully (kept in big-endian format)\n");

    return true;
}

// ========================================
// M68K Initialization
// ========================================

bool CPUContext::init_m68k(const config::EmulatorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    fprintf(stderr, "[CPUContext] ========================================\n");
    fprintf(stderr, "[CPUContext] Initializing M68K CPU context\n");
    fprintf(stderr, "[CPUContext] ========================================\n");

    // If already initialized, shutdown first
    if (state_ != CPUState::UNINITIALIZED) {
        fprintf(stderr, "[CPUContext] Already initialized, shutting down first\n");
        shutdown();
    }

    architecture_ = config::Architecture::M68K;

    // 1. Allocate memory with RAII (unique_ptr auto-frees)
    ram_size_ = config.ram_mb * 1024 * 1024;
    fprintf(stderr, "[CPUContext] Allocating RAM: %u MB\n", config.ram_mb);

    // Allocate RAM + extra space for ROM mapping
    ram_.reset(new (std::nothrow) uint8_t[ram_size_ + 0x100000]);
    if (!ram_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate RAM\n");
        return false;
    }
    memset(ram_.get(), 0, ram_size_);

    // Allocate ROM (max 1MB for M68K)
    rom_.reset(new (std::nothrow) uint8_t[1024 * 1024]);
    if (!rom_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate ROM\n");
        return false;
    }

    // 2. Set up global pointers (for legacy code compatibility)
    // TODO: Remove these when all code uses CPUContext
    RAMBaseHost = ram_.get();
    ROMBaseHost = ram_.get() + ram_size_;  // ROM after RAM
    RAMSize = ram_size_;

#if DIRECT_ADDRESSING
    MEMBaseDiff = (uintptr)RAMBaseHost;
    RAMBaseMac = 0;
    ROMBaseMac = ram_size_;  // Mac address of ROM
#endif

    fprintf(stderr, "[CPUContext] RAM at %p (Mac: 0x%08x)\n", RAMBaseHost, RAMBaseMac);
    fprintf(stderr, "[CPUContext] ROM at %p (Mac: 0x%08x)\n", ROMBaseHost, ROMBaseMac);

    // 3. Load ROM
    if (!load_rom(config.rom_path.c_str())) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to load ROM\n");
        return false;
    }

    // Copy ROM to the ROM area
    memcpy(ROMBaseHost, rom_.get(), rom_size_);
    ROMSize = rom_size_;

    // 4. Check ROM version
    if (!CheckROM()) {
        fprintf(stderr, "[CPUContext] ERROR: Unsupported ROM type\n");
        ErrorAlert(STR_UNSUPPORTED_ROM_TYPE_ERR);
        return false;
    }

    // 5. Determine CPU type from ROM version
#if EMULATED_68K
    switch (ROMVersion) {
        case ROM_VERSION_64K:
        case ROM_VERSION_PLUS:
        case ROM_VERSION_CLASSIC:
            cpu_type_ = 0;  // 68000
            fpu_type_ = 0;
            twenty_four_bit_ = true;
            break;
        case ROM_VERSION_II:
            cpu_type_ = config.cpu_type_int();
            if (cpu_type_ < 2) cpu_type_ = 2;
            if (cpu_type_ > 4) cpu_type_ = 4;
            fpu_type_ = config.fpu ? 1 : 0;
            if (cpu_type_ == 4) fpu_type_ = 1;  // 68040 always with FPU
            twenty_four_bit_ = true;
            break;
        case ROM_VERSION_32:
            cpu_type_ = config.cpu_type_int();
            if (cpu_type_ < 2) cpu_type_ = 2;
            if (cpu_type_ > 4) cpu_type_ = 4;
            fpu_type_ = config.fpu ? 1 : 0;
            if (cpu_type_ == 4) fpu_type_ = 1;  // 68040 always with FPU
            twenty_four_bit_ = false;
            break;
    }
    CPUIs68060 = false;

    // Set global CPU type (for legacy code)
    CPUType = cpu_type_;
    FPUType = fpu_type_;
    TwentyFourBitAddressing = twenty_four_bit_;
#endif

    fprintf(stderr, "[CPUContext] ROM Version: 0x%08x\n", ROMVersion);
    fprintf(stderr, "[CPUContext] CPU Type: 680%02d\n",
            (cpu_type_ == 0) ? 0 : (cpu_type_ * 10 + 20));
    fprintf(stderr, "[CPUContext] FPU: %s\n", fpu_type_ ? "Yes" : "No");
    fprintf(stderr, "[CPUContext] 24-bit addressing: %s\n", twenty_four_bit_ ? "Yes" : "No");

    // 6. Initialize Mac subsystems (XPRAM, drivers, etc.)
    if (!init_mac_subsystems()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to initialize Mac subsystems\n");
        return false;
    }

    // 7. Initialize UAE memory banking (required for all backends currently)
#if EMULATED_68K
    if (!Init680x0()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to initialize 680x0\n");
        return false;
    }
#endif

    // 8. Install CPU backend (platform_ should already have function pointers set by caller)
    // If not set, default to UAE
    if (!platform_.cpu_init) {
        fprintf(stderr, "[CPUContext] No CPU backend set, defaulting to UAE\n");
        cpu_uae_install(&platform_);
    }

    fprintf(stderr, "[CPUContext] CPU Backend: %s\n",
            platform_.cpu_name ? platform_.cpu_name : "Unknown");

    // 9. Configure CPU type
    if (platform_.cpu_set_type) {
        platform_.cpu_set_type(cpu_type_, fpu_type_);
    }

    // 10. Patch ROM
    if (!PatchROM()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to patch ROM\n");
        ErrorAlert(STR_UNSUPPORTED_ROM_TYPE_ERR);
        return false;
    }

    // 11. Initialize CPU backend
    if (!platform_.cpu_init()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to initialize CPU backend\n");
        return false;
    }

    // 12. Reset CPU to ROM entry point
    platform_.cpu_reset();
    fprintf(stderr, "[CPUContext] CPU reset to PC=0x%08x\n", platform_.cpu_get_pc());

    // 13. Set up timer interrupt
    fprintf(stderr, "[CPUContext] Setting up timer interrupt...\n");
    setup_timer_interrupt();

    // Mark as ready
    set_state(CPUState::READY);

    fprintf(stderr, "[CPUContext] ========================================\n");
    fprintf(stderr, "[CPUContext] M68K initialization complete\n");
    fprintf(stderr, "[CPUContext] ========================================\n");

    return true;
}

bool CPUContext::init_mac_subsystems() {
    fprintf(stderr, "[CPUContext] Initializing Mac subsystems...\n");

    // Use shared implementation from emulator_init.cpp
    // (This function is already extracted and works with globals)
    if (!::init_mac_subsystems()) {
        return false;
    }

    fprintf(stderr, "[CPUContext] Mac subsystems initialized\n");
    return true;
}

// ========================================
// PPC Initialization (STUB)
// ========================================

bool CPUContext::init_ppc(const config::EmulatorConfig& config) {
    fprintf(stderr, "[CPUContext] ERROR: PPC emulation not yet implemented\n");
    return false;
}

// ========================================
// Shutdown
// ========================================

void CPUContext::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == CPUState::UNINITIALIZED) {
        return;  // Already shut down
    }

    fprintf(stderr, "[CPUContext] Shutting down...\n");

    // Stop execution if running
    if (state_ == CPUState::RUNNING) {
        set_state(CPUState::READY);
    }

    // Stop timer
    stop_timer_interrupt();

    // Clean up subsystems
    // Note: We don't call ExitAll() here because it exits the process!
    // Individual subsystem cleanup would go here if needed

    // Clear state
    ram_.reset();
    rom_.reset();
    ram_size_ = 0;
    rom_size_ = 0;
    cpu_type_ = 0;
    fpu_type_ = 0;

    set_state(CPUState::UNINITIALIZED);

    fprintf(stderr, "[CPUContext] Shutdown complete\n");
}

// ========================================
// Execution Control
// ========================================

CPUExecResult CPUContext::execute_loop() {
    if (state_ != CPUState::READY && state_ != CPUState::RUNNING) {
        fprintf(stderr, "[CPUContext] ERROR: Cannot execute, state is %d\n",
                static_cast<int>(state_.load()));
        return CPUExecResult::NOT_INITIALIZED;
    }

    set_state(CPUState::RUNNING);
    fprintf(stderr, "[CPUContext] Starting execution loop...\n");

    // Execute until stopped
    if (platform_.cpu_execute_fast) {
        // Fast path (Unicorn, DualCPU) - runs until interrupted
        while (state_ == CPUState::RUNNING) {
            platform_.cpu_execute_one();
        }
    } else {
        // Slow path (UAE) - execute one instruction at a time
        while (state_ == CPUState::RUNNING) {
            platform_.cpu_execute_one();
        }
    }

    fprintf(stderr, "[CPUContext] Execution loop stopped\n");

    CPUState final_state = state_.load();
    if (final_state == CPUState::PAUSED) {
        return CPUExecResult::STOPPED;
    } else if (final_state == CPUState::ERROR) {
        return CPUExecResult::ERROR;
    }

    return CPUExecResult::OK;
}

CPUExecResult CPUContext::execute_one() {
    if (state_ != CPUState::READY && state_ != CPUState::RUNNING) {
        return CPUExecResult::NOT_INITIALIZED;
    }

    platform_.cpu_execute_one();
    return CPUExecResult::OK;
}

void CPUContext::stop() {
    if (state_ == CPUState::RUNNING) {
        set_state(CPUState::READY);
    }
}

void CPUContext::pause() {
    if (state_ == CPUState::RUNNING) {
        set_state(CPUState::PAUSED);
    }
}

void CPUContext::resume() {
    if (state_ == CPUState::PAUSED) {
        set_state(CPUState::RUNNING);
    }
}

void CPUContext::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == CPUState::UNINITIALIZED) {
        fprintf(stderr, "[CPUContext] ERROR: Cannot reset uninitialized context\n");
        return;
    }

    // Stop execution if running
    bool was_running = (state_ == CPUState::RUNNING);
    if (was_running) {
        set_state(CPUState::READY);
        // Wait a bit for execution to stop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "[CPUContext] Resetting CPU...\n");

    // Reset CPU to ROM entry point
    if (platform_.cpu_reset) {
        platform_.cpu_reset();
        fprintf(stderr, "[CPUContext] CPU reset to PC=0x%08x\n",
                platform_.cpu_get_pc ? platform_.cpu_get_pc() : 0);
    }

    // Resume if it was running
    if (was_running) {
        set_state(CPUState::RUNNING);
    }
}

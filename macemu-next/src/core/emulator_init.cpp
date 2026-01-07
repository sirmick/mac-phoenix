/*
 *  emulator_init.cpp - Emulator initialization implementation
 *
 *  Extracted from main.cpp to support deferred initialization.
 */

#include "emulator_init.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "newcpu.h"
#include "memory.h"
#include "main.h"
#include "prefs.h"
#include "video.h"
#include "xpram.h"
#include "timer.h"
#include "rom_patches.h"
#include "user_strings.h"
#include "platform.h"
#include "../platform/timer_interrupt.h"

// Global variables (defined in basilisk_glue.cpp or main.cpp)
extern uint8_t *RAMBaseHost;
extern uint8_t *ROMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMSize;
extern int CPUType;
extern bool CPUIs68060;
extern int FPUType;
extern bool TwentyFourBitAddressing;

// CPU state (from main.cpp)
namespace cpu_state {
    extern std::atomic<bool> g_running;
    extern std::mutex g_mutex;
    extern std::condition_variable g_cv;
}

// Global initialization flag
bool g_emulator_initialized = false;
static std::mutex g_init_mutex;  // Protect initialization

// Load ROM file into memory
bool load_rom_file(const char* rom_path,
                   uint8_t** rom_base_out,
                   uint32_t* rom_size_out)
{
    if (!rom_path || !rom_base_out || !rom_size_out) {
        fprintf(stderr, "[Init] Invalid arguments to load_rom_file\n");
        return false;
    }

    fprintf(stderr, "[Init] Loading ROM from %s...\n", rom_path);

    int rom_fd = open(rom_path, O_RDONLY);
    if (rom_fd < 0) {
        fprintf(stderr, "[Init] Failed to open ROM file: %s\n", rom_path);
        return false;
    }

    // Get ROM size
    off_t size = lseek(rom_fd, 0, SEEK_END);
    if (size < 0) {
        fprintf(stderr, "[Init] Failed to get ROM size\n");
        close(rom_fd);
        return false;
    }

    fprintf(stderr, "[Init] ROM size: %ld bytes (%ld KB)\n", (long)size, (long)size / 1024);

    // Validate ROM size
    if (size != 64*1024 && size != 128*1024 && size != 256*1024 &&
        size != 512*1024 && size != 1024*1024) {
        fprintf(stderr, "[Init] Invalid ROM size (must be 64/128/256/512/1024 KB)\n");
        close(rom_fd);
        return false;
    }

    // Read ROM into memory
    lseek(rom_fd, 0, SEEK_SET);
    ssize_t bytes_read = read(rom_fd, *rom_base_out, size);
    close(rom_fd);

    if (bytes_read != size) {
        fprintf(stderr, "[Init] Failed to read ROM file (read %ld bytes, expected %ld)\n",
                (long)bytes_read, (long)size);
        return false;
    }

    *rom_size_out = (uint32_t)size;
    fprintf(stderr, "[Init] ROM loaded successfully (kept in big-endian format)\n");
    return true;
}

// Initialize CPU subsystem (m68k only for now)
bool init_cpu_subsystem(const char* cpu_backend)
{
    if (!cpu_backend) {
        cpu_backend = "uae";  // Default
    }

    fprintf(stderr, "[Init] Initializing CPU subsystem (backend: %s)...\n", cpu_backend);

    // Check ROM version
    if (!CheckROM()) {
        ErrorAlert(STR_UNSUPPORTED_ROM_TYPE_ERR);
        return false;
    }

#if EMULATED_68K
    // Set CPU and FPU type based on ROM version
    switch (ROMVersion) {
        case ROM_VERSION_64K:
        case ROM_VERSION_PLUS:
        case ROM_VERSION_CLASSIC:
            CPUType = 0;
            FPUType = 0;
            TwentyFourBitAddressing = true;
            break;
        case ROM_VERSION_II:
            CPUType = PrefsFindInt32("cpu");
            if (CPUType < 2) CPUType = 2;
            if (CPUType > 4) CPUType = 4;
            FPUType = PrefsFindBool("fpu") ? 1 : 0;
            if (CPUType == 4) FPUType = 1;	// 68040 always with FPU
            TwentyFourBitAddressing = true;
            break;
        case ROM_VERSION_32:
            CPUType = PrefsFindInt32("cpu");
            if (CPUType < 2) CPUType = 2;
            if (CPUType > 4) CPUType = 4;
            FPUType = PrefsFindBool("fpu") ? 1 : 0;
            if (CPUType == 4) FPUType = 1;	// 68040 always with FPU
            TwentyFourBitAddressing = false;
            break;
    }
    CPUIs68060 = false;

    // Init 680x0 emulation (UAE's memory banking system)
    if (!Init680x0()) {
        fprintf(stderr, "[Init] CPU initialization failed\n");
        return false;
    }
#endif

    // Install CPU backend
    if (strcmp(cpu_backend, "unicorn") == 0) {
        cpu_unicorn_install(&g_platform);
    } else if (strcmp(cpu_backend, "dualcpu") == 0) {
        cpu_dualcpu_install(&g_platform);
    } else {
        cpu_uae_install(&g_platform);  // Default to UAE
    }

    fprintf(stderr, "[Init] CPU Backend: %s\n", g_platform.cpu_name);

    // Configure CPU type (must be called after backend install, before cpu_init)
    if (g_platform.cpu_set_type) {
        g_platform.cpu_set_type(CPUType, FPUType);
    }

    // Install ROM patches
    if (!PatchROM()) {
        ErrorAlert(STR_UNSUPPORTED_ROM_TYPE_ERR);
        return false;
    }

    // Initialize CPU backend
    if (!g_platform.cpu_init()) {
        fprintf(stderr, "[Init] Failed to initialize CPU backend\n");
        return false;
    }

    // Reset CPU to ROM entry point
    g_platform.cpu_reset();
    fprintf(stderr, "[Init] CPU reset to PC=0x%08x\n", g_platform.cpu_get_pc());

    // Set up 60Hz timer (polling-based)
    fprintf(stderr, "[Init] Setting up timer interrupt...\n");
    setup_timer_interrupt();

    fprintf(stderr, "[Init] CPU subsystem initialized successfully\n");
    fprintf(stderr, "[Init] ROM Version: 0x%08x\n", ROMVersion);
    fprintf(stderr, "[Init] CPU Type: 680%02d\n", (CPUType == 0) ? 0 : (CPUType * 10 + 20));
    fprintf(stderr, "[Init] FPU: %s\n", FPUType ? "Yes" : "No");
    fprintf(stderr, "[Init] 24-bit addressing: %s\n", TwentyFourBitAddressing ? "Yes" : "No");

    return true;
}

// Full emulator initialization (ROM + CPU + devices)
bool init_emulator_from_config(const char* emulator_type,
                                const char* storage_dir,
                                const char* rom_filename)
{
    // Thread-safe initialization
    std::lock_guard<std::mutex> lock(g_init_mutex);

    // Check if already initialized
    if (g_emulator_initialized) {
        fprintf(stderr, "[Init] Emulator already initialized\n");
        return true;
    }

    fprintf(stderr, "[Init] ========================================\n");
    fprintf(stderr, "[Init] Starting emulator initialization\n");
    fprintf(stderr, "[Init] Emulator type: %s\n", emulator_type);
    fprintf(stderr, "[Init] Storage dir: %s\n", storage_dir);
    fprintf(stderr, "[Init] ROM filename: %s\n", rom_filename);
    fprintf(stderr, "[Init] ========================================\n");

    // Validate emulator type
    if (strcmp(emulator_type, "m68k") != 0) {
        fprintf(stderr, "[Init] ERROR: Only m68k emulator type is currently supported\n");
        return false;
    }

    // Build ROM path (try absolute first, then relative to storage_dir/roms)
    std::string rom_path;
    struct stat st;
    if (rom_filename[0] == '/' || rom_filename[0] == '~') {
        // Absolute path
        rom_path = rom_filename;
    } else {
        // Relative path - look in storage_dir/roms
        rom_path = std::string(storage_dir) + "/roms/" + rom_filename;
    }

    // Check if ROM file exists
    if (stat(rom_path.c_str(), &st) != 0) {
        fprintf(stderr, "[Init] ERROR: ROM file not found: %s\n", rom_path.c_str());
        return false;
    }

    // Load ROM
    if (!load_rom_file(rom_path.c_str(), &ROMBaseHost, &ROMSize)) {
        fprintf(stderr, "[Init] ERROR: Failed to load ROM file\n");
        return false;
    }

    // Get CPU backend from environment variable
    const char *cpu_backend = getenv("CPU_BACKEND");
    if (!cpu_backend) {
        cpu_backend = "uae";  // Default
    }

    // Initialize CPU subsystem
    if (!init_cpu_subsystem(cpu_backend)) {
        fprintf(stderr, "[Init] ERROR: Failed to initialize CPU subsystem\n");
        return false;
    }

    // Mark as initialized
    g_emulator_initialized = true;
    fprintf(stderr, "[Init] ========================================\n");
    fprintf(stderr, "[Init] Emulator initialization complete!\n");
    fprintf(stderr, "[Init] ========================================\n");

    return true;
}

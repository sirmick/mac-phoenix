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
#include "video.h"
#include "emulator_config.h"
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
#include "rom_patches.h"
#include "user_strings.h"
#include "platform.h"
#include "extfs.h"
#include "../platform/timer_interrupt.h"

#define DEBUG 1
#include "debug.h"

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
        case ROM_VERSION_II: {
            auto& ecfg = config::EmulatorConfig::instance();
            CPUType = ecfg.m68k.cpu_type;
            if (CPUType < 2) CPUType = 2;
            if (CPUType > 4) CPUType = 4;
            FPUType = ecfg.m68k.fpu ? 1 : 0;
            if (CPUType == 4) FPUType = 1;	// 68040 always with FPU
            TwentyFourBitAddressing = true;
            break;
        }
        case ROM_VERSION_32: {
            auto& ecfg = config::EmulatorConfig::instance();
            CPUType = ecfg.m68k.cpu_type;
            if (CPUType < 2) CPUType = 2;
            if (CPUType > 4) CPUType = 4;
            FPUType = ecfg.m68k.fpu ? 1 : 0;
            if (CPUType == 4) FPUType = 1;	// 68040 always with FPU
            TwentyFourBitAddressing = false;
            break;
        }
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

// Initialize Mac subsystems (XPRAM, drivers, audio, video, etc.)
// This is called by both main() and init_emulator_from_config() to avoid duplication
bool init_mac_subsystems(void)
{
    fprintf(stderr, "[Init] Initializing Mac subsystems...\n");

    // Load XPRAM (zap PRAM: skip file load, use fresh defaults)
    auto& cfg = config::EmulatorConfig::instance();
    if (cfg.zappram) {
        fprintf(stderr, "[Init] Zap PRAM: using fresh XPRAM (suppresses improper shutdown dialog)\n");
        memset(XPRAM, 0, XPRAM_SIZE);
    } else {
        XPRAMInit(NULL);
    }

    // Load XPRAM default values if signature not found
    extern uint8 XPRAM[256];
    if (XPRAM[0x0c] != 0x4e || XPRAM[0x0d] != 0x75
     || XPRAM[0x0e] != 0x4d || XPRAM[0x0f] != 0x63) {
        D(bug("Loading XPRAM default values\n"));
        memset(XPRAM, 0, 0x100);
        XPRAM[0x0c] = 0x4e;	// "NuMc" signature
        XPRAM[0x0d] = 0x75;
        XPRAM[0x0e] = 0x4d;
        XPRAM[0x0f] = 0x63;
        XPRAM[0x01] = 0x80;	// InternalWaitFlags = DynWait
        XPRAM[0x10] = 0xa8;	// Standard PRAM values
        XPRAM[0x11] = 0x00;
        XPRAM[0x12] = 0x00;
        XPRAM[0x13] = 0x22;
        XPRAM[0x14] = 0xcc;
        XPRAM[0x15] = 0x0a;
        XPRAM[0x16] = 0xcc;
        XPRAM[0x17] = 0x0a;
        XPRAM[0x1c] = 0x00;
        XPRAM[0x1d] = 0x02;
        XPRAM[0x1e] = 0x63;
        XPRAM[0x1f] = 0x00;
        XPRAM[0x08] = 0x13;
        XPRAM[0x09] = 0x88;
        XPRAM[0x0a] = 0x00;
        XPRAM[0x0b] = 0xcc;
        XPRAM[0x76] = 0x00;	// OSDefault = MacOS
        XPRAM[0x77] = 0x01;
    }

    // Set boot volume
    int16 i16 = cfg.bootdrive;
    XPRAM[0x78] = i16 >> 8;
    XPRAM[0x79] = i16 & 0xff;
    i16 = cfg.bootdriver;
    XPRAM[0x7a] = i16 >> 8;
    XPRAM[0x7b] = i16 & 0xff;

    // Init drivers
    SonyInit();
    DiskInit();
    CDROMInit();
    SCSIInit();

#if SUPPORTS_EXTFS
    // Init external file system
    ExtFSInit();
#endif

    // Init serial ports
    SerialInit();

    // Init network
    EtherInit();

    // Init Time Manager
    TimerInit();

    // Init clipboard
    ClipInit();

    // Init ADB
    ADBInit();

    // Init audio
    AudioInit();

    // Init video
    extern uint16 ROMVersion;
    if (!VideoInit(ROMVersion == ROM_VERSION_64K || ROMVersion == ROM_VERSION_PLUS || ROMVersion == ROM_VERSION_CLASSIC)) {
        fprintf(stderr, "[Init] ERROR: Video initialization failed\n");
        return false;
    }

    // Set default video mode in XPRAM
    XPRAM[0x56] = 0x42;	// 'B'
    XPRAM[0x57] = 0x32;	// '2'
    extern std::vector<monitor_desc *> VideoMonitors;
    const monitor_desc &main_monitor = *VideoMonitors[0];
    XPRAM[0x58] = uint8(main_monitor.depth_to_apple_mode(main_monitor.get_current_mode().depth));
    XPRAM[0x59] = 0;

    fprintf(stderr, "[Init] Mac subsystems initialized successfully\n");
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

    // Initialize Mac subsystems (XPRAM, drivers, audio, video, etc.)
    // MUST be done before CPU init because PatchROM() may use these
    if (!init_mac_subsystems()) {
        fprintf(stderr, "[Init] ERROR: Failed to initialize Mac subsystems\n");
        return false;
    }

    // Get CPU backend from environment variable
    const char *cpu_backend = getenv("CPU_BACKEND");
    if (!cpu_backend) {
        cpu_backend = "uae";  // Default
    }

    // Initialize CPU subsystem (includes PatchROM and cpu_init)
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

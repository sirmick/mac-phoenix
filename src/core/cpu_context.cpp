/*
 *  cpu_context.cpp - CPU execution context implementation
 */

#include "cpu_context.h"
#include "emulator_init.h"
#include "main.h"
#include "rom_patches.h"
#include "rom_patches_ppc.h"
#include "ppc_constants.h"
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

// Forward declarations for external variables (used pervasively by legacy code)
extern uint8_t *RAMBaseHost;
extern uint8_t *ROMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMSize;
extern int CPUType;
extern int FPUType;
extern bool CPUIs68060;
extern bool TwentyFourBitAddressing;
extern uint16 ROMVersion;
extern uint8 *ScratchMem;

static const int SCRATCH_MEM_SIZE = 0x10000;  // 64KB scratch memory for ROM HW base patching
static const int FRAMEBUFFER_AREA_SIZE = 0x800000;  // 8MB reserved for frame buffer after ScratchMem (supports up to 1920x1080x32)

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
    // Platform will be initialized by caller
    // (cannot copy g_platform here because this constructor runs before main())
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

    // Validate ROM size (M68K: 64K-1MB, PPC: up to 4MB)
    bool valid_size = (size == 64*1024 || size == 128*1024 || size == 256*1024 ||
                       size == 512*1024 || size == 1024*1024 ||
                       size == 3*1024*1024 || size == 4*1024*1024);
    if (!valid_size) {
        fprintf(stderr, "[CPUContext] ERROR: Invalid ROM size %ld (must be 64/128/256/512/1024/3072/4096 KB)\n",
                (long)size / 1024);
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

    // Allocate RAM + ROM (1MB) + ScratchMem (64KB) + FrameBuffer area (4MB)
    // Layout: [RAM 32MB][ROM 1MB][ScratchMem 64KB][FrameBuffer 4MB]
    // Frame buffer MUST be outside RAM to avoid overlapping Mac heap data structures
    ram_.reset(new (std::nothrow) uint8_t[ram_size_ + 0x100000 + SCRATCH_MEM_SIZE + FRAMEBUFFER_AREA_SIZE]);
    if (!ram_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate RAM\n");
        return false;
    }
    memset(ram_.get(), 0, ram_size_ + 0x100000 + SCRATCH_MEM_SIZE + FRAMEBUFFER_AREA_SIZE);

    // Allocate ROM (max 1MB for M68K)
    rom_.reset(new (std::nothrow) uint8_t[1024 * 1024]);
    if (!rom_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate ROM\n");
        return false;
    }

    // 2. Set up global pointers (for legacy code compatibility)
    // These globals are referenced by ~250 sites across 24 files (UAE interpreter,
    // ROM patches, memory accessors, video drivers, etc.) — refactoring them out
    // would require rewriting the entire memory access layer.
    RAMBaseHost = ram_.get();
    ROMBaseHost = ram_.get() + ram_size_;  // ROM after RAM
    RAMSize = ram_size_;

#if DIRECT_ADDRESSING
    MEMBaseDiff = (uintptr)RAMBaseHost;
    RAMBaseMac = 0;
    ROMBaseMac = Host2MacAddr(ROMBaseHost);
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

    // Allocate ScratchMem right after ROM in the contiguous buffer.
    // do_get_real_address() accepts addresses up to ROMBaseMac + ROMSize + 0x10000.
    // ScratchMem pointer is in the middle of the scratch block (original convention).
    ScratchMem = ROMBaseHost + ROMSize + SCRATCH_MEM_SIZE / 2;
    fprintf(stderr, "[CPUContext] ScratchMem at %p (Mac: 0x%08x)\n",
            ScratchMem, Host2MacAddr(ScratchMem));

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
            fpu_type_ = config.fpu() ? 1 : 0;
            if (cpu_type_ == 4) fpu_type_ = 1;  // 68040 always with FPU
            twenty_four_bit_ = true;
            break;
        case ROM_VERSION_32:
            cpu_type_ = config.cpu_type_int();
            if (cpu_type_ < 2) cpu_type_ = 2;
            if (cpu_type_ > 4) cpu_type_ = 4;
            // 512KB ROMs (IIci etc.) have 68030 — cap at 3 to avoid
            // 68040 RTE frame format mismatches in ROM boot code
            if (ROMSize <= 0x80000 && cpu_type_ > 3) cpu_type_ = 3;
            fpu_type_ = config.fpu() ? 1 : 0;
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
    fprintf(stderr, "[CPUContext] CPU Type: 680%d0\n", cpu_type_);
    fprintf(stderr, "[CPUContext] FPU: %s\n", fpu_type_ ? "Yes" : "No");
    fprintf(stderr, "[CPUContext] 24-bit addressing: %s\n", twenty_four_bit_ ? "Yes" : "No");

    // 6. Log storage config
    if (config.bootdriver != 0) {
        fprintf(stderr, "[CPUContext] Boot driver override: %d\n", config.bootdriver);
    }
    for (const auto& disk_path : config.disk_paths) {
        fprintf(stderr, "[CPUContext] Disk: %s\n", disk_path.c_str());
    }
    for (const auto& cdrom_path : config.cdrom_paths) {
        fprintf(stderr, "[CPUContext] CDROM: %s\n", cdrom_path.c_str());
    }

    // 7. Initialize Mac subsystems (XPRAM, drivers, etc.)
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
// PPC Initialization
// ========================================

// PPC memory mapping (defined in cpu_ppc_unicorn.cpp)
extern bool ppc_unicorn_map_memory(void);

// PPC ROM constants now in ppc_constants.h

bool CPUContext::init_ppc(const config::EmulatorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    fprintf(stderr, "[CPUContext] ========================================\n");
    fprintf(stderr, "[CPUContext] Initializing PPC CPU context\n");
    fprintf(stderr, "[CPUContext] ========================================\n");

    if (state_ != CPUState::UNINITIALIZED) {
        fprintf(stderr, "[CPUContext] Already initialized, shutting down first\n");
        shutdown();
    }

    architecture_ = config::Architecture::PPC;

    // 1. Allocate memory
    // PPC layout: RAM from 0x0, ROM at 0x400000 (inside RAM space)
    // Allocate enough for RAM + ScratchMem + FrameBuffer
    ram_size_ = config.ram_mb * 1024 * 1024;
    fprintf(stderr, "[CPUContext] Allocating RAM: %u MB\n", config.ram_mb);

    // Total allocation: RAM + ScratchMem + FrameBuffer
    // ROM lives at 0x400000 within the RAM buffer (PPC Macs map ROM into address space)
    size_t total_size = ram_size_ + SCRATCH_MEM_SIZE + FRAMEBUFFER_AREA_SIZE;
    ram_.reset(new (std::nothrow) uint8_t[total_size]);
    if (!ram_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate RAM\n");
        return false;
    }
    memset(ram_.get(), 0, total_size);

    // Allocate ROM read buffer (max 4MB for PPC)
    rom_.reset(new (std::nothrow) uint8_t[4 * 1024 * 1024]);
    if (!rom_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate ROM buffer\n");
        return false;
    }

    // 2. Set up global pointers
    RAMBaseHost = ram_.get();
    RAMSize = ram_size_;

    // PPC: ROM is inside RAM at offset 0x400000
    // ROMBaseHost points to the ROM data within the RAM buffer
    ROMBaseHost = ram_.get() + PPC_ROM_BASE;

#if DIRECT_ADDRESSING
    MEMBaseDiff = (uintptr)RAMBaseHost;
    RAMBaseMac = 0;
    ROMBaseMac = PPC_ROM_BASE;
#endif

    fprintf(stderr, "[CPUContext] RAM at %p (Mac: 0x%08x), %u MB\n",
            RAMBaseHost, RAMBaseMac, config.ram_mb);
    fprintf(stderr, "[CPUContext] ROM area at %p (Mac: 0x%08x)\n",
            ROMBaseHost, ROMBaseMac);

    // 3. Load ROM
    if (!load_rom(config.rom_path.c_str())) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to load ROM\n");
        return false;
    }

    // PPC ROM validation: Gossamer ROMs are typically 4MB
    if (rom_size_ != 4 * 1024 * 1024 && rom_size_ != 3 * 1024 * 1024) {
        fprintf(stderr, "[CPUContext] WARNING: PPC ROM is %u bytes (expected 3-4MB for Gossamer)\n",
                rom_size_);
    }

    // Copy ROM into RAM at 0x400000
    memcpy(ROMBaseHost, rom_.get(), rom_size_);
    ROMSize = rom_size_;

    fprintf(stderr, "[CPUContext] ROM loaded at Mac 0x%08x (%u KB)\n",
            PPC_ROM_BASE, rom_size_ / 1024);

    // ScratchMem after RAM (for compatibility with drivers)
    ScratchMem = ram_.get() + ram_size_ + SCRATCH_MEM_SIZE / 2;

    // 4. CPU type (PPC 750 = G3)
    cpu_type_ = 4;  // Not m68k, but keep for compatibility
    fpu_type_ = 1;  // PPC always has FPU
    twenty_four_bit_ = false;

    CPUType = cpu_type_;
    FPUType = fpu_type_;
    TwentyFourBitAddressing = false;

    fprintf(stderr, "[CPUContext] CPU: PowerPC 750 (G3)\n");

    // 5. Log storage config
    for (const auto& disk_path : config.disk_paths) {
        fprintf(stderr, "[CPUContext] Disk: %s\n", disk_path.c_str());
    }
    for (const auto& cdrom_path : config.cdrom_paths) {
        fprintf(stderr, "[CPUContext] CDROM: %s\n", cdrom_path.c_str());
    }

    // 6. Initialize Mac subsystems (XPRAM, drivers, etc.)
    if (!init_mac_subsystems()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to initialize Mac subsystems\n");
        return false;
    }

    // 7. Initialize XLM globals and patch ROM
    fprintf(stderr, "[CPUContext] Initializing PPC XLM globals...\n");
    InitXLM();

    fprintf(stderr, "[CPUContext] Patching PPC ROM...\n");
    if (!PatchROM_PPC()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to patch PPC ROM\n");
        return false;
    }

    // 8. Initialize CPU backend
    fprintf(stderr, "[CPUContext] CPU Backend: %s\n",
            platform_.cpu_name ? platform_.cpu_name : "Unknown");

    if (platform_.cpu_set_type) {
        platform_.cpu_set_type(cpu_type_, fpu_type_);
    }

    if (!platform_.cpu_init()) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to initialize CPU backend\n");
        return false;
    }

    // 9. Map memory into CPU engine (Unicorn needs explicit mapping)
    if (config.cpu_backend == config::CPUBackend::Unicorn ||
        config.cpu_backend == config::CPUBackend::DualCPU) {
        if (!ppc_unicorn_map_memory()) {
            fprintf(stderr, "[CPUContext] ERROR: Failed to map PPC memory\n");
            return false;
        }
    }

    // 10. Reset CPU
    platform_.cpu_reset();
    fprintf(stderr, "[CPUContext] CPU reset, PC=0x%08x\n", platform_.cpu_get_pc());

    // 11. Set PC to ROM entry point (nanokernel reset vector at ROM + 0x310000)
    uint32_t rom_entry = PPC_ROM_BASE + 0x310000;
    if (platform_.cpu_ppc_set_pc) {
        platform_.cpu_ppc_set_pc(rom_entry);
        fprintf(stderr, "[CPUContext] PPC entry point set to 0x%08x\n", rom_entry);
    }

    // Set initial PPC registers for nanokernel entry
    // Based on SheepShaver's sheepshaver_glue.cpp and ppc_asm.S:
    //   r3 = ROM base + 0x30d000 (nanokernel info block)
    //   r4 = KernelData + 0x1000 (emulator data area)
    if (platform_.cpu_ppc_set_gpr) {
        platform_.cpu_ppc_set_gpr(3, PPC_ROM_BASE + 0x30d000);
        platform_.cpu_ppc_set_gpr(4, KERNEL_DATA_BASE + 0x1000);
        fprintf(stderr, "[CPUContext] PPC initial registers: r3=0x%08x r4=0x%08x\n",
                (uint32_t)(PPC_ROM_BASE + 0x30d000), (uint32_t)(KERNEL_DATA_BASE + 0x1000));
    }

    // 11. Set up timer interrupt
    fprintf(stderr, "[CPUContext] Setting up timer interrupt...\n");
    setup_timer_interrupt();

    set_state(CPUState::READY);

    fprintf(stderr, "[CPUContext] ========================================\n");
    fprintf(stderr, "[CPUContext] PPC initialization complete\n");
    fprintf(stderr, "[CPUContext] ========================================\n");

    return true;
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

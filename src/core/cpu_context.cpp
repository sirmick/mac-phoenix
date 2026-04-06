/*
 *  cpu_context.cpp - CPU execution context implementation
 */

#include "cpu_context.h"
#include "emulator_init.h"
#include "main.h"
#include "rom_patches.h"
using namespace m68k;
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
#include <sys/ipc.h>
#include <sys/shm.h>
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

    // 1. Load ROM to temp buffer first — we need to peek the version
    //    before deciding on memory layout (24-bit ROMs need ROM at $400000).
    rom_.reset(new (std::nothrow) uint8_t[1024 * 1024]);
    if (!rom_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate ROM buffer\n");
        return false;
    }
    if (!load_rom(config.rom_path.c_str())) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to load ROM\n");
        return false;
    }

    // Peek ROM version from temp buffer to determine memory layout
    uint16_t rom_version_peek = ntohs(*(uint16_t *)(rom_.get() + 8));

    // 2. Determine memory layout based on ROM version
    ram_size_ = config.ram_mb * 1024 * 1024;
    uint32_t rom_offset;  // offset of ROM within the mmap block

    if (rom_version_peek == ROM_VERSION_CLASSIC || rom_version_peek == ROM_VERSION_II) {
        // 24-bit ROMs: ROM lives at Mac $400000 (right after max 4MB RAM).
        // Clamp RAM to 4MB — SE/Plus/Classic hardware limit.
        if (ram_size_ > 0x00400000) {
            fprintf(stderr, "[CPUContext] Clamping RAM to 4 MB for 24-bit ROM\n");
            ram_size_ = 0x00400000;
        }
        rom_offset = 0x00400000;
    } else {
        // 32-bit ROMs: ROM immediately follows RAM
        rom_offset = ram_size_;
    }

    fprintf(stderr, "[CPUContext] Allocating RAM: %u MB\n", ram_size_ / (1024 * 1024));

    // Allocate contiguous block: [RAM...][ROM 1MB][ScratchMem 64KB][FrameBuffer 4MB]
    // For 24-bit ROMs, rom_offset may exceed ram_size_ (gap is unmapped Mac space).
    size_t total_alloc = rom_offset + 0x100000 + SCRATCH_MEM_SIZE + FRAMEBUFFER_AREA_SIZE;

    // JIT requires MEMBaseDiff to fit in a 32-bit x86 displacement, so allocate
    // in the low 32-bit address space. Fall back to heap if MAP_32BIT fails.
    mmap_ram_ = (uint8_t*)mmap(nullptr, total_alloc,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
        -1, 0);
    if (mmap_ram_ != MAP_FAILED) {
        mmap_ram_size_ = total_alloc;
        memset(mmap_ram_, 0, total_alloc);
        fprintf(stderr, "[CPUContext] RAM mmap'd at %p (low 32-bit, JIT OK)\n", mmap_ram_);
    } else {
        mmap_ram_ = nullptr;
        fprintf(stderr, "[CPUContext] WARNING: MAP_32BIT mmap failed, falling back to heap (JIT disabled)\n");
        ram_.reset(new (std::nothrow) uint8_t[total_alloc]);
        if (!ram_) {
            fprintf(stderr, "[CPUContext] ERROR: Failed to allocate RAM\n");
            return false;
        }
        memset(ram_.get(), 0, total_alloc);
    }

    // 3. Set up global pointers (for legacy code compatibility)
    uint8_t* ram_base = mmap_ram_ ? mmap_ram_ : ram_.get();
    RAMBaseHost = ram_base;
    ROMBaseHost = ram_base + rom_offset;
    RAMSize = ram_size_;

    MEMBaseDiff = (uintptr)RAMBaseHost;
    RAMBaseMac = 0;
    ROMBaseMac = rom_offset;

    fprintf(stderr, "[CPUContext] RAM at %p (Mac: 0x%08x, %u MB)\n",
            RAMBaseHost, RAMBaseMac, ram_size_ / (1024 * 1024));
    fprintf(stderr, "[CPUContext] ROM at %p (Mac: 0x%08x)\n", ROMBaseHost, ROMBaseMac);

    // Copy ROM from temp buffer to final location
    memcpy(ROMBaseHost, rom_.get(), rom_size_);
    ROMSize = rom_size_;

    // Allocate ScratchMem right after ROM in the contiguous buffer.
    // do_get_real_address() accepts addresses up to ROMBaseMac + ROMSize + 0x10000.
    // ScratchMem pointer is in the middle of the scratch block (original convention).
    ScratchMem = ROMBaseHost + ROMSize + SCRATCH_MEM_SIZE / 2;
    fprintf(stderr, "[CPUContext] ScratchMem at %p (Mac: 0x%08x)\n",
            ScratchMem, (uint32)(ScratchMem - RAMBaseHost));

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
    fprintf(stderr, "[CPUContext] CPU Type: 680%02d\n",
            (cpu_type_ == 0) ? 0 : (cpu_type_ * 10 + 20));
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

    fprintf(stderr, "[CPUContext] CPU Backend: %s (JIT: %s)\n",
            platform_.cpu_name ? platform_.cpu_name : "Unknown",
            config.m68k.jitexperimental ? "on" : "off");

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

// Externs from KPX bridge (cpu_ppc_kpx.cpp) — ppc:: namespace
extern "C" void cpu_ppc_kpx_install(Platform *p);
namespace ppc {
    extern uint32_t RAMBase, RAMSize, ROMBase, KernelDataAddr;
    extern uint8_t *RAMBaseHost, *ROMBaseHost;
}
// Note: do NOT add using directives here — RAMBaseHost/ROMBaseHost/RAMSize
// are also declared as globals (basilisk_glue.cpp) for m68k. Use ppc:: prefix
// in init_ppc() to access the PPC versions.
extern "C" bool kpx_sheep_mem_init(void);
extern "C" void kpx_set_signal_stack(uintptr_t addr);
extern uint32_t PVR;
extern uint32_t TimebaseSpeed;

// PPC memory constants (from SheepShaver cpu_emulation.h)
static const uint32_t KERNEL_DATA_BASE = 0x68ffe000;

// PPC RAM/ROM pointers: ppc:: namespace, declared above with using directives.

bool CPUContext::init_ppc(const config::EmulatorConfig& config) {
    using ppc::RAMBase; using ppc::RAMSize; using ppc::ROMBase;
    using ppc::RAMBaseHost; using ppc::ROMBaseHost; using ppc::KernelDataAddr;
    std::lock_guard<std::mutex> lock(mutex_);

    fprintf(stderr, "[CPUContext] ========================================\n");
    fprintf(stderr, "[CPUContext] Initializing PPC CPU context\n");
    fprintf(stderr, "[CPUContext] ========================================\n");

    if (state_ != CPUState::UNINITIALIZED) {
        fprintf(stderr, "[CPUContext] Already initialized, shutting down first\n");
        shutdown();
    }

    architecture_ = config::Architecture::PPC;

    // 1. Allocate memory using REAL_ADDRESSING (SheepShaver approach)
    // Mac address == host address, VMBaseDiff = 0.
    // RAM+ROM allocated contiguously, mapped at whatever address the OS gives us.
    // Requires vm.mmap_min_addr=0 for low memory access.
    ram_size_ = config.ram_mb * 1024 * 1024;
    if (ram_size_ < 16 * 1024 * 1024) ram_size_ = 16 * 1024 * 1024;
    const uint32_t ppc_rom_size = 0x400000;  // 4MB PPC ROM
    const uint32_t ROM_AREA_SIZE = 0x500000; // 5MB ROM area
    const uint32_t SIG_STACK_SIZE = 0x10000; // 64KB signal stack

    // Allocate RAM at address 0, with extra padding above for nanokernel probing.
    // The Gossamer ROM nanokernel at ROM+0x310000 probes memory above RAMSize during
    // init to detect physical memory topology and build page tables. Legacy SheepShaver
    // allocates RAM+ROM+align+sig as one contiguous block, leaving ~6MB accessible above
    // RAM. Without this padding, reads above RAM cause SIGSEGV → silently skipped →
    // nanokernel skips page table setup and initializes KD incorrectly (KD+0x65c wrong
    // offset, KD+0x660 corrupted flags, KD+0x920 page table empty).
    // See docs/ppc/nanokernel_init_divergence.md for full diagnosis.
    const size_t NK_PROBE_PAD = 8 * 1024 * 1024;  // 8MB for nanokernel probing
    RAMBaseHost = (uint8_t *)mmap((void *)0, ram_size_ + NK_PROBE_PAD,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (RAMBaseHost == MAP_FAILED) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to map RAM at address 0\n");
        fprintf(stderr, "[CPUContext] (Run: sudo sysctl vm.mmap_min_addr=0)\n");
        return false;
    }

    // REAL_ADDRESSING: Mac address = host address (VMBaseDiff = 0)
    RAMBase = (uint32_t)(uintptr_t)RAMBaseHost;
    RAMSize = ram_size_;

    // Map ROM separately at a high address (matching SheepShaver layout)
    // SheepShaver puts ROM at ~0x50000000, NOT contiguous with RAM.
    // This ensures all ROM-relative nanokernel addresses match SheepShaver's.
    const uint32_t ROM_BASE_ADDR = 0x50000000;
    void *rom_area = mmap((void *)ROM_BASE_ADDR, ROM_AREA_SIZE + SIG_STACK_SIZE,
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (rom_area == MAP_FAILED) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to map ROM at 0x%08x\n", ROM_BASE_ADDR);
        return false;
    }
    ROMBase = ROM_BASE_ADDR;
    ROMBaseHost = (uint8_t *)(uintptr_t)ROMBase;

    // Set signal stack at end of ROM area (matching legacy: sig_stack = ROMEnd)
    kpx_set_signal_stack(ROM_BASE_ADDR + ROM_AREA_SIZE);

    fprintf(stderr, "[CPUContext] REAL_ADDRESSING mode (VMBaseDiff=0)\n");
    fprintf(stderr, "[TRACE] RAM: base=0x%08x size=0x%08x host=%p\n", RAMBase, RAMSize, RAMBaseHost);
    fprintf(stderr, "[TRACE] ROM: base=0x%08x size=0x%08x host=%p contiguous=0\n", ROMBase, ROM_AREA_SIZE, ROMBaseHost);

    // Low Memory (0x0000..0x3000) is covered by RAM at address 0
    // Map Kernel Data at both 0x68ffe000 and 0x5fffe000 using shared memory
    // so writes to either address are visible from both (SheepShaver kernel_data_init)
    {
        int kd_size = 0x2000;
        int kd_shm = shmget(IPC_PRIVATE, kd_size, IPC_CREAT | 0600);
        if (kd_shm != -1) {
            void *kd1 = shmat(kd_shm, (void *)KERNEL_DATA_BASE, 0);
            void *kd2 = shmat(kd_shm, (void *)0x5fffe000, 0);
            shmctl(kd_shm, IPC_RMID, NULL);
            if (kd1 == (void *)KERNEL_DATA_BASE && kd2 == (void *)0x5fffe000) {
                fprintf(stderr, "[CPUContext] KernelData shared at 0x68ffe000 and 0x5fffe000\n");
            } else {
                fprintf(stderr, "[CPUContext] WARNING: KernelData shmat failed, using separate maps\n");
                if (kd1 != (void *)-1) shmdt(kd1);
                if (kd2 != (void *)-1) shmdt(kd2);
                mmap((void *)KERNEL_DATA_BASE, kd_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
                mmap((void *)0x5fffe000, kd_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            }
        } else {
            fprintf(stderr, "[CPUContext] WARNING: shmget failed, using mmap for KernelData\n");
            mmap((void *)KERNEL_DATA_BASE, kd_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            mmap((void *)0x5fffe000, kd_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        }
    }

    // NOTE: DR emulator (0x68070000) and DR cache (0x69000000) are mapped LATER,
    // after nanokernel init. Mapping them here changes what the nanokernel reads
    // during its init phase, causing KD+0x65c and KD+0x660 to get wrong values.
    // Legacy SheepShaver doesn't map these regions at all — the nanokernel's
    // SIGSEGV handler silently skips reads from unmapped addresses.


    // Allocate ROM loading buffer
    rom_.reset(new (std::nothrow) uint8_t[ppc_rom_size]);
    if (!rom_) {
        fprintf(stderr, "[CPUContext] ERROR: Failed to allocate ROM buffer\n");
        return false;
    }

    // 3. Load ROM (plain 4MB or CHRP compressed NewWorld ROMs)
    if (!config.rom_path.empty()) {
        fprintf(stderr, "[CPUContext] Loading ROM from: %s\n", config.rom_path.c_str());

        int rom_fd = open(config.rom_path.c_str(), O_RDONLY);
        if (rom_fd < 0) {
            fprintf(stderr, "[CPUContext] ERROR: Failed to open ROM file: %s\n",
                    config.rom_path.c_str());
            return false;
        }

        off_t size = lseek(rom_fd, 0, SEEK_END);
        lseek(rom_fd, 0, SEEK_SET);

        fprintf(stderr, "[CPUContext] ROM size: %ld bytes (%ld KB)\n",
                (long)size, (long)size / 1024);

        if (size <= 0 || size > (off_t)ppc_rom_size) {
            fprintf(stderr, "[CPUContext] ERROR: ROM file too large or empty (got %ld bytes, max %u)\n",
                    (long)size, ppc_rom_size);
            close(rom_fd);
            return false;
        }

        // Read ROM into temp buffer (may be smaller than 4MB for CHRP compressed ROMs)
        ssize_t bytes_read = read(rom_fd, rom_.get(), size);
        close(rom_fd);

        if (bytes_read != size) {
            fprintf(stderr, "[CPUContext] ERROR: Failed to read ROM\n");
            return false;
        }

        rom_size_ = ppc_rom_size;
        ROMSize = rom_size_;  // Set global for PatchROM()

        // DecodeROM handles both plain 4MB ROMs and CHRP compressed (NewWorld) ROMs.
        // It decompresses into ROMBaseHost (already allocated in the ROM memory region).
        extern bool DecodeROM(uint8 *data, uint32 size);
        if (!DecodeROM(rom_.get(), (uint32)bytes_read)) {
            if (bytes_read != (ssize_t)ppc_rom_size) {
                fprintf(stderr, "[CPUContext] ERROR: ROM decoding failed (not a valid 4MB or CHRP ROM)\n");
            } else {
                fprintf(stderr, "[CPUContext] ERROR: ROM file appears corrupt\n");
            }
            return false;
        }
        fprintf(stderr, "[CPUContext] ROM loaded and decoded successfully\n");
    } else {
        fprintf(stderr, "[CPUContext] ERROR: No ROM path specified\n");
        return false;
    }

    // 4. Set up KernelData area (mapped above at 0x68ffe000)
    KernelDataAddr = KERNEL_DATA_BASE;
    fprintf(stderr, "[CPUContext] KernelData at 0x%08x\n", KernelDataAddr);

    // DR Emulator and DR Cache are mapped AFTER InitAll_PPC (step 7c below).
    // They MUST NOT exist during nanokernel init — legacy SheepShaver doesn't
    // map them, and the nanokernel's init code probes 0x68070000/0x69000000.
    // If these regions are mapped, the nanokernel reads zeros instead of
    // faulting, causing KD+0x65c and KD+0x660 to get wrong values.

    // 5b. Initialize VM subsystem (open /dev/zero) BEFORE SheepMem.
    //     Legacy calls vm_init() from main() before SheepMem::Init().
    //     This ensures the decode cache and JIT allocate at the same addresses.
    {
        extern int vm_init(void);
        vm_init();
    }

    // 5c. Initialize SheepMem (shared memory for Mac/host communication)
    //     Must be done before InitAll() because ThunksInit() uses SheepMem::Reserve()
    {
        // SheepMem::Init is declared in thunks.h (KPX compat), implemented in ppc_memory.cpp
        // Can't include thunks.h here (wrong memory model), so use extern C++ linkage
        if (!kpx_sheep_mem_init()) {
            fprintf(stderr, "[CPUContext] ERROR: SheepMem::Init failed\n");
            return false;
        }
    }

    // 6. Install KPX backend (function pointers only — CPU instance created
    //    later in kpx_cpu_init, which only runs in the child subprocess)
    cpu_ppc_kpx_install(&platform_);
    platform_.ppc_jit = config.ppc.jit;
    fprintf(stderr, "[CPUContext] CPU Backend: %s (JIT: %s)\n", platform_.cpu_name, config.ppc.jit ? "on" : "off");

    // 6b. Sync g_platform NOW so core code (disk.cpp, cdrom.cpp, extfs.cpp etc.)
    //     can use g_platform.mem_read_long / cpu_execute_68k_trap during InitAll_PPC.
    //     Without this, core code falls through to uninitialized UAE banking fallbacks.
    {
        extern Platform g_platform;
        g_platform = platform_;
    }

    // 7. Call SheepShaver's InitAll() — patches ROM, sets up KernelData,
    //    initializes XLM globals, sets up thunks, initializes drivers.
    //    This is the exact sequence from SheepShaver main.cpp.
    {
        extern bool InitAll_PPC(const char *vmdir);
        fprintf(stderr, "[TRACE] InitAll: enter\n");
        if (!InitAll_PPC(nullptr)) {
            fprintf(stderr, "[CPUContext] ERROR: InitAll failed\n");
            return false;
        }
        fprintf(stderr, "[TRACE] InitAll: exit\n");
    }

    // 7b. Flush code cache after ROM patching (SheepShaver main_unix.cpp:1155)
    // The JIT may have cached unpatched ROM code — flush it so patches take effect.
    {
        extern void FlushCodeCache(uintptr start, uintptr end);
        FlushCodeCache(ROMBase, ROMBase + ROM_AREA_SIZE);
        fprintf(stderr, "[CPUContext] ROM code cache flushed\n");
    }

    // 7c. NOW map DR Emulator and DR Cache (after nanokernel init completed).
    // These must not exist during InitAll_PPC → PatchROM → nanokernel init.
    {
        const uintptr_t DR_EMUL_BASE = 0x68070000;
        const uint32_t  DR_EMUL_SIZE = 0x10000;
        const uintptr_t DR_CACH_BASE = 0x69000000;
        const uint32_t  DR_CACH_SIZE = 0x80000;

        void *dr_emu = mmap((void *)DR_EMUL_BASE, DR_EMUL_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        void *dr_cache = mmap((void *)DR_CACH_BASE, DR_CACH_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (dr_emu == MAP_FAILED || dr_cache == MAP_FAILED) {
            fprintf(stderr, "[CPUContext] WARNING: Failed to map DR regions (emu=%p, cache=%p)\n",
                    dr_emu, dr_cache);
        } else {
            fprintf(stderr, "[CPUContext] DR Emulator at %p (64KB), DR Cache at %p (512KB)\n",
                    dr_emu, dr_cache);
        }
    }

    // 7d. Write-protect ROM (SheepShaver main_unix.cpp:1157)
    // Legacy protects ROM_AREA_SIZE (5MB). We match this.
    // Writes to 0x50400000+ after init are silently dropped by SIGSEGV handler.
    {
        uint32_t protect_size = ROM_AREA_SIZE;  // 5MB, matching legacy
        if (mprotect(ROMBaseHost, protect_size, PROT_READ | PROT_EXEC) < 0) {
            fprintf(stderr, "[CPUContext] WARNING: Could not write-protect ROM\n");
        } else {
            fprintf(stderr, "[CPUContext] ROM write-protected (%d KB, opcode table area at +0x%x remains writable)\n",
                    protect_size / 1024, protect_size);
        }
    }

    // 8. Initialize PPC CPU state (GPR3, GPR4, MODE_68K)
    // Equivalent to SheepShaver's init_emul_ppc()
    if (platform_.cpu_init) {
        platform_.cpu_init();
    }

    // 9. Re-sync g_platform after cpu_init (may have updated function pointers)
    {
        extern Platform g_platform;
        g_platform = platform_;
    }

    // 10. Timer interrupt: PPC uses its own tick thread in cpu_ppc_kpx.cpp,
    //     so we skip setup_timer_interrupt() (polling timer unused by KPX).
    //     M68K backends use setup_timer_interrupt() from their own init paths.

    // Mark as ready
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
    if (mmap_ram_) {
        munmap(mmap_ram_, mmap_ram_size_);
        mmap_ram_ = nullptr;
        mmap_ram_size_ = 0;
    }
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

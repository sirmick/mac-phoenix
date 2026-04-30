// Stub KPX PPC backend for non-x86 hosts.
//
// The real KPX uses SheepShaver's dyngen, which embeds precompiled x86/x86_64
// machine code in dyngen_precompiled/*.hpp. That can't build on riscv64/arm,
// so this stub provides just the symbols cpu_context.cpp links against and
// makes PPC mode fail at init time with a clear message.

#include <cstdio>
#include <cstdint>
#include <cstdlib>

#include "platform.h"

namespace ppc {
    uint32_t RAMBase = 0;
    uint32_t RAMSize = 0;
    uint8_t *RAMBaseHost = nullptr;
    uint32_t ROMBase = 0;
    uint8_t *ROMBaseHost = nullptr;
    uint32_t KernelDataAddr = 0;
    // Shared with cpu_unicorn_ppc.cpp — defined in real KPX's
    // video_ppc.cpp. Stub for non-x86 hosts where Unicorn-PPC may
    // still load (Unicorn supports PPC on any host) but never reaches
    // a working framebuffer because KPX init bailed out first.
    uint32_t screen_base = 0;
}

// SheepMem state — defined in real KPX's ppc_memory.cpp. Stubs let
// cpu_unicorn_ppc.cpp link on arm64; reads return zero, writes are
// no-ops, so the Unicorn-PPC paths log warnings and proceed.
uintptr_t SheepMem_base = 0;
uintptr_t SheepMem_proc = 0;
uintptr_t SheepMem_data = 0;

// Framebuffer hooks — real impls live in kpx/video_ppc.cpp. Stubs
// return null/zero so cpu_unicorn_ppc.cpp's "framebuffer not yet
// allocated" warning fires instead of crashing.
extern "C" uint8_t *video_ppc_get_framebuffer_host(void) { return nullptr; }
extern "C" uint32_t video_ppc_get_framebuffer_size(void) { return 0; }

extern "C" void cpu_ppc_kpx_install(Platform *p) {
    (void)p;
    fprintf(stderr,
            "[KPX] PPC backend not available on this host architecture "
            "(dyngen requires x86/x86_64 precompiled blobs). "
            "Run with --backend uae.\n");
    std::exit(2);
}

extern "C" bool kpx_sheep_mem_init(void) { return false; }
extern "C" void kpx_set_signal_stack(uintptr_t) {}
extern "C" uint32_t kpx_sheep_mem_reserve(uint32_t) { return 0; }

// Additional PPC-path symbols referenced from core on the non-PPC link.
// These are never reached because cpu_ppc_kpx_install() exits first, but
// the linker needs a definition.
bool DecodeROM(uint8_t *, uint32_t) { return false; }
bool InitAll_PPC(const char *) { return false; }
void FlushCodeCache(uintptr_t, uintptr_t) {}
uint64_t ppc_insn_counter = 0;

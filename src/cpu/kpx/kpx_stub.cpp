// Stub KPX backend for non-x86 hosts.
//
// The real KPX uses SheepShaver's dyngen, whose precompiled machine code
// in dyngen_precompiled/*.hpp is x86/x86_64-only. On other arches the
// dyngen-based interpreter (cpu_ppc_kpx.cpp + ppc-{cpu,execute,decode,
// translate}.cpp) is replaced by this single-function stub.
//
// All shared PPC-mode state (SheepMem, RAM/ROM base, PVR, clock speeds,
// ppc::EmulOp, framebuffer, ROM/resource patches, name registry,
// gfxaccel, serial, ether) lives in `kpx_shared` and compiles on every
// arch — see src/cpu/kpx/CMakeLists.txt.
//
// What stays here: tiny stubs for the call_macos* family — these
// dispatch into the dyngen interpreter and only have a real
// implementation in cpu_ppc_kpx.cpp on x86. On non-x86,
// kpx_shared code that calls them (serial_ppc, name_registry_ppc, etc.)
// will link against these no-op stubs. They are never reached at
// runtime because cpu_ppc_kpx_install() exits below before any Mac OS
// code can dispatch.

#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "platform.h"

extern "C" void cpu_ppc_kpx_install(Platform *p) {
    (void)p;
    fprintf(stderr,
            "[KPX] PPC backend not available on this host architecture "
            "(dyngen requires x86/x86_64 precompiled blobs). "
            "Run with --backend uae.\n");
    std::exit(2);
}

// KPX-runtime internal hooks. Real implementations in cpu_ppc_kpx.cpp
// + src/cpu/ppc/ppc-cpu.cpp on x86. arm64 link sees these no-ops; all
// are unreachable at runtime because cpu_ppc_kpx_install exits before
// the KPX runtime gets installed.


// JIT code cache invalidation hook.
void FlushCodeCache(uintptr_t /*start*/, uintptr_t /*end*/) {}

// PPC native-op dispatch — called from generated code; signature must
// match cpu_ppc_kpx.cpp:708.
extern "C" void kpx_ppc_native_op(uint32_t /*selector*/, uint32_t /*gprs*/[32]) {}

// Per-instruction counter — definition lives in src/cpu/ppc/ppc-cpu.cpp
// on x86 (KPX interpreter). Stub here so name_registry / kernel-data
// stamping reads zero on non-x86.
uint64_t ppc_insn_counter = 0;

// call_macos* family — run Mac OS code (a TVECT) from the host. Kheperix's
// versions live in cpu_ppc_kpx.cpp (x86 only); here they go to the PPC core
// that is installed, as cpu_ppc_kpx.cpp does when Kheperix is absent
// (mame-ppc's ppc_execute_macos_code). They used to return 0 without
// running anything, which left the Name Registry, the video driver and the
// rest of the host-to-Mac calls undone on arm64: the boot stopped early.
static uint32_t dispatch_macos_code(uint32_t tvect, int nargs, const uint32_t *args)
{
    if (g_platform.ppc_execute_macos_code)
        return g_platform.ppc_execute_macos_code(tvect, nargs, args);
    fprintf(stderr, "[call_macos] no PPC core for tvect=0x%08x\n", tvect);
    return 0;
}
uint32_t call_macos (uint32_t t)                                                                                   { return dispatch_macos_code(t, 0, nullptr); }
uint32_t call_macos1(uint32_t t, uint32_t a1)                                                                      { const uint32_t a[] = {a1}; return dispatch_macos_code(t, 1, a); }
uint32_t call_macos2(uint32_t t, uint32_t a1, uint32_t a2)                                                         { const uint32_t a[] = {a1,a2}; return dispatch_macos_code(t, 2, a); }
uint32_t call_macos3(uint32_t t, uint32_t a1, uint32_t a2, uint32_t a3)                                            { const uint32_t a[] = {a1,a2,a3}; return dispatch_macos_code(t, 3, a); }
uint32_t call_macos4(uint32_t t, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)                               { const uint32_t a[] = {a1,a2,a3,a4}; return dispatch_macos_code(t, 4, a); }
uint32_t call_macos5(uint32_t t, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)                  { const uint32_t a[] = {a1,a2,a3,a4,a5}; return dispatch_macos_code(t, 5, a); }
uint32_t call_macos6(uint32_t t, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)     { const uint32_t a[] = {a1,a2,a3,a4,a5,a6}; return dispatch_macos_code(t, 6, a); }
uint32_t call_macos7(uint32_t t, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { const uint32_t a[] = {a1,a2,a3,a4,a5,a6,a7}; return dispatch_macos_code(t, 7, a); }

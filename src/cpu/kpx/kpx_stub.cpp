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
// will link against these no-op stubs. unicorn-ppc never reaches them
// at runtime because cpu_ppc_kpx_install() exits below before any
// Mac OS code can dispatch.

#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "platform.h"

extern "C" void cpu_ppc_kpx_install(Platform *p) {
    (void)p;
    fprintf(stderr,
            "[KPX] PPC backend not available on this host architecture "
            "(dyngen requires x86/x86_64 precompiled blobs). "
            "Run with --backend uae or --backend unicorn-ppc.\n");
    std::exit(2);
}

// call_macos* family — dispatches Mac OS code through the PPC interpreter.
// Real C++ implementations in cpu_ppc_kpx.cpp on x86; arm64 link sees
// these no-ops, which is fine because kpx is unreachable at runtime
// (cpu_ppc_kpx_install above exits before any Mac OS dispatch).
// Signatures match cpu_ppc_kpx.cpp's `uint32 call_macosN(uint32, ...)`
// — uint32 is a typedef of uint32_t in compat/sysdeps.h, so the mangled
// symbol names line up.
uint32_t call_macos (uint32_t)                                                                                       { return 0; }
uint32_t call_macos1(uint32_t, uint32_t)                                                                             { return 0; }
uint32_t call_macos2(uint32_t, uint32_t, uint32_t)                                                                   { return 0; }
uint32_t call_macos3(uint32_t, uint32_t, uint32_t, uint32_t)                                                         { return 0; }
uint32_t call_macos4(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)                                               { return 0; }
uint32_t call_macos5(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)                                     { return 0; }
uint32_t call_macos6(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)                           { return 0; }
uint32_t call_macos7(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)                 { return 0; }

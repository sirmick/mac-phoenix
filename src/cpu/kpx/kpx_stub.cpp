// Stub KPX backend for non-x86 hosts.
//
// The real KPX uses SheepShaver's dyngen, whose precompiled machine code
// in dyngen_precompiled/*.hpp is x86/x86_64-only. On other arches the
// dyngen-based interpreter (cpu_ppc_kpx.cpp + ppc-{cpu,execute,decode,
// translate}.cpp) is replaced by this single-function stub.
//
// All the shared PPC-mode infrastructure (SheepMem, video framebuffer,
// ROM/resource patches, ppc::EmulOp, name registry, gfxaccel, serial,
// ether, etc.) lives in the `kpx_shared` library, which compiles on
// every arch — see src/cpu/kpx/CMakeLists.txt. cpu_unicorn_ppc.cpp gets
// real symbols from kpx_shared instead of the empty stubs that used to
// live here.

#include <cstdio>
#include <cstdlib>

#include "platform.h"

extern "C" void cpu_ppc_kpx_install(Platform *p) {
    (void)p;
    fprintf(stderr,
            "[KPX] PPC backend not available on this host architecture "
            "(dyngen requires x86/x86_64 precompiled blobs). "
            "Run with --backend uae or --backend unicorn-ppc.\n");
    std::exit(2);
}

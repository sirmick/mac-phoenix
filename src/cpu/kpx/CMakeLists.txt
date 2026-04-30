# KPX (Kheperix) PPC CPU with dyngen JIT + bridge + boot
#
# Split into two groups: interpreter core (needs -O1 for performance)
# and dyngen files (must use -O0 due to precompiled machine code ICE at -O2).

# Shared PPC-mode Mac OS infrastructure. Defines SheepMem state,
# `ppc::EmulOp`, `ppc::ROMType`, `ppc::screen_base`, framebuffer
# accessors, signal stack, ROM/resource patches, and the Mac OS
# subsystem helpers (name registry, gfxaccel, serial, ether). None of
# these reach into dyngen — they implement Mac OS via KPX's vm.hpp
# direct-memory model and the platform abstraction. Compiles on every
# host arch so cpu_unicorn_ppc.cpp gets the same symbols on arm64
# that it does on x86, and matches how src/core/{cpu_context.cpp,
# emul_op.cpp} hold the equivalent shared state for the m68k mode.
set(KPX_SHARED_SOURCES
  rom_patches_ppc.cpp
  emul_op_ppc.cpp
  rsrc_patches_ppc.cpp
  ppc_memory.cpp
  thunks_ppc.cpp
  init_ppc.cpp
  video_ppc.cpp
  gfxaccel_ppc.cpp
  macos_util_ppc.cpp
  name_registry_ppc.cpp
  serial_ppc.cpp
  compat/vm_alloc.cpp
  compat/ppc_ether.cpp
)

# KPX dyngen-based interpreter. x86-only because dyngen embeds
# precompiled x86 machine code in dyngen_precompiled/*.hpp.
set(KPX_INTERPRETER_SOURCES
  src/cpu/ppc/ppc-cpu.cpp
  src/cpu/ppc/ppc-execute.cpp
  src/cpu/ppc/ppc-decode.cpp
  src/cpu/ppc/ppc-translate.cpp
  src/utils/utils-cpuinfo.cpp
  src/mathlib/mathlib.cpp
  src/mathlib/ieeefp.cpp
  cpu_ppc_kpx.cpp
)

set(KPX_DYNGEN_SOURCES
  src/cpu/ppc/ppc-jit.cpp
  src/cpu/ppc/ppc-dyngen.cpp
  src/cpu/jit/jit-cache.cpp
  src/cpu/jit/basic-dyngen.cpp
  src/cpu/jit/cxxdemangle.cpp
)

set(KPX_INC
  ${CMAKE_CURRENT_SOURCE_DIR}/compat
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/dyngen_precompiled
)

set(KPX_COMMON_INC
  ${KPX_INC}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/video
  ${CMAKE_CURRENT_SOURCE_DIR}/../../config
  ${CMAKE_CURRENT_SOURCE_DIR}/../../core
)

set(KPX_CPP_ARGS
  -DSHEEPSHAVER=1
  -DEMULATED_PPC=1
  -DPPC_ENABLE_JIT=1
  -DPPC_REENTRANT_JIT=1
  -DPPC_CHECK_INTERRUPTS=0
  -DPPC_DECODE_CACHE=1
  -DPPC_FLIGHT_RECORDER=1
  -DREAL_ADDRESSING=1
  -DKPX_MAX_CPUS=1
  -DENABLE_DYNGEN=1
  -DENABLE_MON=0
  -DUSE_ETHER_FULL_DRIVER=1
  -DHAVE_SIGSEGV_SKIP_INSTRUCTION=1
  -DHAVE_LINKER_SCRIPT=1
  -D_GNU_SOURCE
  -DHAVE_CONFIG_H
  -DHAVE_FCNTL_H
  -w
  -DPPC_DETERMINISTIC_REPLAY=1
)

# Shared PPC infrastructure — every arch builds this. cpu_unicorn_ppc.cpp
# links against it and so does kpx_interp on x86. Same -O1 + KPX_CPP_ARGS
# as the interpreter so the Mac-OS helper code keeps its tuning.
add_library(kpx_shared STATIC ${KPX_SHARED_SOURCES})
target_include_directories(kpx_shared PRIVATE ${KPX_COMMON_INC})
target_link_libraries(kpx_shared PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(kpx_shared PRIVATE ${KPX_CPP_ARGS} -O1)

# KPX relies on SheepShaver's dyngen, whose precompiled machine code in
# dyngen_precompiled/*.hpp is x86/x86_64-only. On other arches we build a
# stub so the link succeeds; PPC mode at runtime prints an error and exits.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
  # Interpreter core at -O1 (hot loop needs optimization)
  add_library(kpx_interp STATIC ${KPX_INTERPRETER_SOURCES})
  target_include_directories(kpx_interp PRIVATE ${KPX_COMMON_INC})
  target_link_libraries(kpx_interp PRIVATE nlohmann_json::nlohmann_json kpx_shared)
  target_compile_options(kpx_interp PRIVATE ${KPX_CPP_ARGS} -O1)

  # Dyngen/JIT at -O0 (precompiled machine code in headers causes ICE at -O2)
  add_library(kpx_dyngen STATIC ${KPX_DYNGEN_SOURCES})
  target_include_directories(kpx_dyngen PRIVATE ${KPX_COMMON_INC})
  target_link_libraries(kpx_dyngen PRIVATE nlohmann_json::nlohmann_json)
  target_compile_options(kpx_dyngen PRIVATE ${KPX_CPP_ARGS})
else()
  # Tiny stub: cpu_ppc_kpx_install() prints "use --backend uae" and exits.
  # All the real PPC-mode symbols come from kpx_shared above.
  add_library(kpx_interp STATIC kpx_stub.cpp)
  target_include_directories(kpx_interp PRIVATE ${KPX_COMMON_INC})
  target_link_libraries(kpx_interp PRIVATE kpx_shared)
  add_library(kpx_dyngen STATIC kpx_stub_empty.cpp)
endif()

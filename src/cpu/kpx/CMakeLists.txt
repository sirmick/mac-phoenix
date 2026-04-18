# KPX (Kheperix) PPC CPU with dyngen JIT + bridge + boot
#
# Split into two groups: interpreter core (needs -O1 for performance)
# and dyngen files (must use -O0 due to precompiled machine code ICE at -O2).

set(KPX_INTERPRETER_SOURCES
  src/cpu/ppc/ppc-cpu.cpp
  src/cpu/ppc/ppc-execute.cpp
  src/cpu/ppc/ppc-decode.cpp
  src/cpu/ppc/ppc-translate.cpp
  src/utils/utils-cpuinfo.cpp
  src/mathlib/mathlib.cpp
  src/mathlib/ieeefp.cpp
  cpu_ppc_kpx.cpp
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

# Interpreter core at -O1 (hot loop needs optimization)
add_library(kpx_interp STATIC ${KPX_INTERPRETER_SOURCES})
target_include_directories(kpx_interp PRIVATE ${KPX_COMMON_INC})
target_compile_options(kpx_interp PRIVATE ${KPX_CPP_ARGS} -O1)

# Dyngen/JIT at -O0 (precompiled machine code in headers causes ICE at -O2)
add_library(kpx_dyngen STATIC ${KPX_DYNGEN_SOURCES})
target_include_directories(kpx_dyngen PRIVATE ${KPX_COMMON_INC})
target_compile_options(kpx_dyngen PRIVATE ${KPX_CPP_ARGS})

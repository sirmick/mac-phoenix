# UAE CPU Core Build
#
# IMPORTANT: Like BasiliskII, we compile cpuemu.cpp AND cpustbl.cpp TWICE:
# 1. Without -DNOFLAGS to get op_xxx_ff functions and op_smalltbl_X_ff tables
# 2. With -DNOFLAGS to get op_xxx_nf functions and op_smalltbl_X_nf tables

# UAE JIT only has x86/x86_64 code generators; disable on other arches
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
  set(UAE_USE_JIT 1)
else()
  set(UAE_USE_JIT 0)
endif()

set(UAE_COMMON_ARGS
  -DCPU_EMU_SIZE=0
  -DREAL_ADDRESSING=0
  -DDIRECT_ADDRESSING=1
  -DUSE_JIT=${UAE_USE_JIT}
  -DUSE_JIT_FPU=0
  -DUSE_PREFETCH_BUFFER=0
  -DFPU_IEEE
  -Wno-unused-variable
  -Wno-unused-parameter
  -Wno-unused-function
  -Wno-sign-compare
  -Wno-unused-but-set-variable
  -Wno-parentheses
  -Wno-format
  -Wno-narrowing
  -Wno-unused-label
  -Wno-shift-negative-value
)

set(UAE_INC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/compiler
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_SOURCE_DIR}/../kpx/compat
)

# --- cpuemu_ff: compiled WITHOUT -DNOFLAGS ---
add_library(cpuemu_ff STATIC cpuemu_ff.cpp)
target_include_directories(cpuemu_ff PRIVATE ${UAE_INC})
target_compile_options(cpuemu_ff PRIVATE ${UAE_COMMON_ARGS})

# --- cpustbl_ff: compiled WITHOUT -DNOFLAGS ---
add_library(cpustbl_ff STATIC cpustbl.cpp)
target_include_directories(cpustbl_ff PRIVATE ${UAE_INC})
target_compile_options(cpustbl_ff PRIVATE ${UAE_COMMON_ARGS})

# --- vm_alloc ---
add_library(vm_alloc STATIC ../kpx/compat/vm_alloc.cpp)
target_include_directories(vm_alloc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../kpx/compat)
target_compile_options(vm_alloc PRIVATE
  -DHAVE_MMAP_VM=1
  -DHAVE_MMAP_ANONYMOUS=1
  -DHAVE_UNISTD_H=1
  -DHAVE_SYS_MMAN_H=1
  -DHAVE_FCNTL_H=1
  -Wno-unused-parameter
)

# --- JIT compiler ---
add_library(uae_jit STATIC
  compiler/compemu_support.cpp
  compiler/compemu_fpp.cpp
  compiler/compemu.cpp
  compiler/compstbl.cpp
)
target_include_directories(uae_jit PRIVATE ${UAE_INC})
# compemu_support.cpp transitively includes config/emulator_config.h which
# pulls in <QJsonObject>. Link Qt6::Core for the include path.
find_package(Qt6 6.4 COMPONENTS Core REQUIRED)
target_link_libraries(uae_jit PRIVATE Qt6::Core)
target_compile_options(uae_jit PRIVATE
  ${UAE_COMMON_ARGS}
  -fpermissive
  -Wno-missing-field-initializers
)

# --- Main UAE CPU lib: compiled WITH -DNOFLAGS ---
add_library(uae_cpu STATIC
  newcpu.cpp
  cpuemu_nf.cpp
  cpustbl_nf.cpp
  cpudefs.cpp
  readcpu.cpp
  memory.cpp
  basilisk_glue.cpp
  basilisk_stubs.cpp
  fpu/fpu_ieee.cpp
)
target_include_directories(uae_cpu PRIVATE
  ${UAE_INC}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/platform
)
target_compile_options(uae_cpu PRIVATE ${UAE_COMMON_ARGS} -DNOFLAGS)

# Individual targets (uae_cpu, uae_jit, vm_alloc, cpuemu_ff, cpustbl_ff)
# are linked at the top-level CMakeLists.txt with --start-group and --whole-archive

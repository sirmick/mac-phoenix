# cpu::Core (cpu_core.h): one interface for every CPU core. Each core is
# its own translation unit with its own headers; registry.cpp creates them
# by name.

add_library(cpu_core STATIC
    guest_memory.cpp
    registry.cpp
    platform_cpu.cpp
    ${PROJECT_SOURCE_DIR}/src/common/guest_watch.cpp   # --watch (guest_watch.h), for every binary with a core
)
target_include_directories(cpu_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/common/include)
target_link_libraries(cpu_core PUBLIC mame_m68k mame_ppc)
target_compile_features(cpu_core PUBLIC cxx_std_17)

if(EMSCRIPTEN)
    return()  # no ROM machine, no UAE
endif()

# UAE: a library of its own, for the ROM machine only (registry.cpp,
# register_uae()); Executor and the test binaries link cpu_core without it.
# Compiled with UAE's own flags and include path (both cores have an
# m68k.h). Its symbols (regs, init_m68k, g_platform) come from the UAE
# libraries and src/core at the final link.
add_library(cpu_core_uae STATIC uae_core.cpp)
target_link_libraries(cpu_core_uae PUBLIC cpu_core)
target_include_directories(cpu_core_uae PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../uae_cpu
    ${CMAKE_CURRENT_SOURCE_DIR}/../uae_cpu/compiler
    ${PROJECT_SOURCE_DIR}/src/common/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../kpx/compat)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
    set(_core_uae_jit 1)
else()
    set(_core_uae_jit 0)
endif()
target_compile_options(cpu_core_uae PRIVATE
    -DCPU_EMU_SIZE=0 -DREAL_ADDRESSING=0 -DDIRECT_ADDRESSING=1
    -DUSE_JIT=${_core_uae_jit} -DUSE_JIT_FPU=0 -DUSE_PREFETCH_BUFFER=0 -DFPU_IEEE
    -Wno-unused-parameter -Wno-sign-compare)
target_compile_features(cpu_core_uae PRIVATE cxx_std_17)

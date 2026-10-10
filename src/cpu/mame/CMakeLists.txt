# MAME CPU cores lifted into MacPhoenix (docs/cpu/PLAN.md). BSD-3-Clause;
# LICENSE-MAME. Reference checkout: /home/mick/src/mame-src (master
# a025aba, 2026-10-09). The framework the cores expect is stood in for by
# shim/emu.h; imported files are edited as little as possible and every
# edit is marked "MacPhoenix" and listed in docs/cpu/PLAN.md.

set(MAME_SHIM_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/util   # strformat.h, endianness.h, mfpresolve.h ...
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party        # softfloat3/..., util/...
    ${CMAKE_CURRENT_SOURCE_DIR}/../core            # cpu_core.h
    ${PROJECT_SOURCE_DIR}/src/common/include)

set(MAME_WARNINGS -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
    -Wno-unused-but-set-variable)

# The shim and the lifted utility sources.
add_library(mame_shim STATIC
    shim/emu.cpp
    shim/osdlib.cpp
    third_party/util/mfpresolve.cpp
    third_party/util/strformat.cpp
    third_party/util/vecstream.cpp)
target_include_directories(mame_shim PUBLIC ${MAME_SHIM_INC})
target_compile_definitions(mame_shim PUBLIC LSB_FIRST)
target_compile_options(mame_shim PRIVATE -O2 ${MAME_WARNINGS})
target_compile_features(mame_shim PUBLIC cxx_std_20)

# softfloat3 (BSD-3, John R. Hauser) with MAME's bochs_ext additions, the
# 68881/68040 FPU's arithmetic. Compiled as C++, as MAME does (the
# extensions use references).
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3_sources.cmake)
add_library(mame_softfloat3 STATIC ${SOFTFLOAT3_SOURCES})
set_source_files_properties(${SOFTFLOAT3_SOURCES} PROPERTIES LANGUAGE CXX)
target_include_directories(mame_softfloat3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3/build/MAME
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3/source
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3/source/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3/source/8086
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3/bochs_ext)
target_compile_definitions(mame_softfloat3 PRIVATE
    SOFTFLOAT_ROUND_ODD INLINE_LEVEL=5 SOFTFLOAT_FAST_DIV32TO16
    SOFTFLOAT_FAST_DIV64TO32 SOFTFLOAT_FAST_INT64 LSB_FIRST LITTLEENDIAN=1)
target_compile_options(mame_softfloat3 PRIVATE -w -O2)
target_compile_features(mame_softfloat3 PRIVATE cxx_std_20)

# The 68020-68040 core (Musashi, Karl Stenerud; PMMU by R. Belmont and
# others) and its cpu::Core adapter. m68kops.cpp is generated from
# m68k_in.lst by m68kmake.py and committed; the generator is not a build
# requirement.
add_library(mame_m68k STATIC
    m68000/m68kcpu.cpp
    m68000/m68kops.cpp
    m68000/m68kfpu.cpp
    m68000/m68kcommon.cpp
    m68000/m68kdasm.cpp
    mame_68k_core.cpp)
target_include_directories(mame_m68k PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/m68000
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/softfloat3/build/MAME)
target_compile_options(mame_m68k PRIVATE -O2 ${MAME_WARNINGS})
target_link_libraries(mame_m68k PUBLIC mame_shim PRIVATE mame_softfloat3)

# asmjit (zlib licence; LICENSE-asmjit), the native back-ends' assembler.
# MAME's copy, all targets compiled; the back-end picks x86 or a64.
file(GLOB_RECURSE ASMJIT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit/asmjit/*.cpp)
add_library(mame_asmjit STATIC ${ASMJIT_SOURCES})
target_include_directories(mame_asmjit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asmjit)
target_compile_definitions(mame_asmjit PUBLIC ASMJIT_STATIC)
target_compile_options(mame_asmjit PRIVATE -w -O2)
target_compile_features(mame_asmjit PRIVATE cxx_std_20)

# The PowerPC core (ppccom/ppcdrc/ppcfe) on MAME's UML recompiler: the
# UML core, the C back-end (drcbec) everywhere, the x86-64 back-end on
# x86-64 (arm64: C2d). divtlb is MAME's virtual TLB mix-in, taken as is.
set(MAME_PPC_SOURCES
    powerpc/ppccom.cpp
    powerpc/ppcdrc.cpp
    powerpc/ppcfe.cpp
    powerpc/ppc_dasm.cpp
    cpu/uml.cpp
    cpu/drcuml.cpp
    cpu/drccache.cpp
    cpu/drcbeut.cpp
    cpu/drcbec.cpp
    emu/divtlb.cpp
    mame_ppc_core.cpp)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    list(APPEND MAME_PPC_SOURCES cpu/drcbex64.cpp cpu/x86log.cpp)
    set(MAME_PPC_NATIVE "")                       # drcuml.cpp picks drcbe_x64
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    # C2d: lifted but not yet built or run on an arm64 host (no such host
    # here). drcbearm64.cpp needs the same execute() re-entrancy treatment
    # as drcbex64.cpp (saved stack pointer in near state) before it works
    # with nested runs; see docs/cpu/PLAN.md.
    list(APPEND MAME_PPC_SOURCES cpu/drcbearm64.cpp)
    set(MAME_PPC_NATIVE "")                       # drcuml.cpp picks drcbe_arm64
else()
    set(MAME_PPC_NATIVE NATIVE_DRC=drcbe_c)
endif()
add_library(mame_ppc STATIC ${MAME_PPC_SOURCES})
target_include_directories(mame_ppc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # "cpu/drcuml.h", "asmjit/asmjit/x86.h"
    ${CMAKE_CURRENT_SOURCE_DIR}/cpu      # "drcuml.h"
    ${CMAKE_CURRENT_SOURCE_DIR}/emu      # "divtlb.h"
    ${CMAKE_CURRENT_SOURCE_DIR}/powerpc)
target_compile_definitions(mame_ppc PRIVATE ${MAME_PPC_NATIVE})
target_compile_options(mame_ppc PRIVATE -O2 ${MAME_WARNINGS})
target_link_libraries(mame_ppc PUBLIC mame_shim PRIVATE mame_asmjit)

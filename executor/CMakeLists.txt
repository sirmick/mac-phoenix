# =============================================================================
# Executor core (Executor 2000, MIT) on MAME's Musashi (mame-68k).
# See docs/executor/PLAN.md.
#
#   executor_cpu          syn68k API facade over a cpu::Core + PowerCore on mame-ppc
#   romlib                the imported Toolbox
#   front-end-phoenix     video/cursor front end + executor_host API,
#                         linked into mac-phoenix (`--backend executor`)
#   executor_tests        Executor's gtest suite, on mame-68k
# =============================================================================

# A C++17 compiler is all Executor needs: its generated sources are
# committed (romlib/generated; -DEXECUTOR_REGENERATE=ON remakes them with
# ruby, perl and bison).
find_package(Threads REQUIRED)

# Guest address 0 is ordinary data to the Toolbox (low memory globals,
# nil handles dereferenced on purpose). Never let the compiler treat a
# guest-to-host conversion of 0 as UB.
set(EXECUTOR_COMMON_FLAGS -fno-delete-null-pointer-checks)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeRC.cmake)
cmrc_add_resource_library(resources
        WHENCE core/res
        core/res/System
        core/res/System.ad
        core/res/Browser
        core/res/Browser.ad
        core/res/Printer
        core/res/Printer.ad
        core/res/godata.sav
        core/res/printdef.ini
        core/res/printers.ini
        core/res/about/1_License.txt
        core/res/about/2_Credits.txt
        core/res/about/3_Tips.txt
        core/res/resource-policy.txt
        core/res/extension-policy.txt
    )

# --- CPU facade --------------------------------------------------------------
# syn68k_common.cpp implements the syn68k API on a cpu::Core
# (src/cpu/core), chosen at startup (--core): mame-68k.
add_library(executor_cpu STATIC
    core/cpu/syn68k_common.cpp
    core/cpu/PowerCore.cpp
    # syn68k_host.h: the guest windows' memory
    $<IF:$<BOOL:${EMSCRIPTEN}>,core/cpu/host/wasm/memory.cpp,core/cpu/host/posix/memory.cpp>
)
target_include_directories(executor_cpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/cpu)
target_include_directories(executor_cpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/romlib)   # base/trap_history.h
target_compile_options(executor_cpu PRIVATE ${EXECUTOR_COMMON_FLAGS})
target_link_libraries(executor_cpu PUBLIC cpu_core Threads::Threads)
phoenix_exempt_from_hardening(executor_cpu)

# --- Toolbox -------------------------------------------------------------------
add_subdirectory(core/romlib)   # and the front ends (frontends/)
target_compile_options(romlib PRIVATE ${EXECUTOR_COMMON_FLAGS} -Wno-deprecated-declarations)
# --logtraps support (cheap when the flag is off).
target_compile_definitions(romlib PUBLIC EXECUTOR_ENABLE_LOGGING)

# High-resolution rendering (docs/executor/HiDPI.md, --executor-scale):
# Skia, BSD-3-Clause, prebuilt static libraries (JetBrains' skia-pack m144:
# tools/skia/fetch.sh puts it where SKIA_DIR looks by default). Off by
# default; without it romlib/hidpi/hidpi.h is inline no-ops.
option(EXECUTOR_SKIA "Executor's high-resolution rendering, with Skia (docs/executor/HiDPI.md)" OFF)
if(EXECUTOR_SKIA)
    if(EMSCRIPTEN)
        set(_skia_target wasm-wasm)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_skia_target linux-arm64)
    else()
        set(_skia_target linux-x64)
    endif()
    set(SKIA_DIR "$ENV{HOME}/storage/deps/skia-m144-${_skia_target}" CACHE PATH
        "Skia: headers and out/Release-<target>/ (tools/skia/fetch.sh)")
    # Emscripten's toolchain file restricts find_library to its own sysroot
    # (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY) unless told otherwise; SKIA_DIR
    # is outside it either way.
    find_library(SKIA_LIB skia PATHS "${SKIA_DIR}/out/Release-${_skia_target}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SKIA_FREETYPE_LIB freetype2 PATHS "${SKIA_DIR}/out/Release-${_skia_target}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT SKIA_LIB OR NOT SKIA_FREETYPE_LIB OR NOT EXISTS "${SKIA_DIR}/include/core/SkCanvas.h")
        message(FATAL_ERROR "EXECUTOR_SKIA: no Skia in ${SKIA_DIR} (tools/skia/fetch.sh, or -DSKIA_DIR=...)")
    endif()
    target_sources(romlib PRIVATE core/romlib/hidpi/hidpi.cpp core/romlib/hidpi/hidpi_skia.cpp)
    # Skia's headers for the one file that draws (its include root has
    # include/ and src/ directories of its own)
    set_source_files_properties(core/romlib/hidpi/hidpi_skia.cpp TARGET_DIRECTORY romlib PROPERTIES
        INCLUDE_DIRECTORIES "${SKIA_DIR}")
    target_compile_definitions(romlib PUBLIC EXECUTOR_HIDPI=1)
    # FreeType inside Skia is its own static copy (FTL), not the system's
    target_link_libraries(romlib ${SKIA_LIB} ${SKIA_FREETYPE_LIB})
    # FreeType's BUILD.gn always depends on libpng (for color-emoji glyphs)
    # and that in turn on zlib; skcms is Skia's own color-management
    # support. The native/arm64 prebuilt's link gets these transitively
    # from the system zlib/libpng that Qt already needs (CLAUDE.md); the
    # wasm build has no such thing linked in, so tools/skia/build-wasm.sh's
    # output needs them listed explicitly (harmless, if present but
    # unreferenced: a static archive only pulls the object files a symbol
    # reference actually needs).
    find_library(SKIA_PNG_LIB png PATHS "${SKIA_DIR}/out/Release-${_skia_target}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SKIA_ZLIB_LIB zlib PATHS "${SKIA_DIR}/out/Release-${_skia_target}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SKIA_SKCMS_LIB skcms PATHS "${SKIA_DIR}/out/Release-${_skia_target}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    foreach(_extra SKIA_PNG_LIB SKIA_ZLIB_LIB SKIA_SKCMS_LIB)
        if(${_extra})
            target_link_libraries(romlib ${${_extra}})
        endif()
    endforeach()
    message(STATUS "Executor: high-resolution rendering with Skia from ${SKIA_DIR}")
endif()
phoenix_exempt_from_hardening(romlib)
phoenix_exempt_from_hardening(front-end-phoenix)
target_compile_options(front-end-phoenix PRIVATE ${EXECUTOR_COMMON_FLAGS})

# Into the one binary (its g_platform comes from src/core). Not in the
# wasm build, which has no mac-phoenix.
if(TARGET mac-phoenix)
    target_sources(mac-phoenix PRIVATE frontends/phoenix/executor_child.cpp)
    target_link_libraries(mac-phoenix PRIVATE front-end-phoenix)
    target_compile_definitions(mac-phoenix PRIVATE HAVE_EXECUTOR=1)
    target_include_directories(mac-phoenix PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/frontends/phoenix)
endif()

# g_platform for the test binaries, which have no src/core. Compiled into
# each consumer (an archive would sit before the libraries that use it).
add_library(executor_platform_glue INTERFACE)
target_sources(executor_platform_glue INTERFACE ${PROJECT_SOURCE_DIR}/src/common/platform_glue.cpp)
target_include_directories(executor_platform_glue INTERFACE ${PROJECT_SOURCE_DIR}/src/common/include)
target_link_libraries(executor_platform_glue INTERFACE executor_cpu cpu_core Threads::Threads)

if(EMSCRIPTEN)
    set(BUILD_TESTS ON)
endif()

# --- Tests ---------------------------------------------------------------------
if(BUILD_TESTS)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(GOOGLETEST_VERSION 1.9.0)
    add_subdirectory(core/third_party/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)

    if(TARGET mac-phoenix)
        add_test(NAME executor.smoke.web_backend
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_web_backend.sh
                    $<TARGET_FILE:mac-phoenix> ${CMAKE_CURRENT_BINARY_DIR}/smoke)
        set_tests_properties(executor.smoke.web_backend PROPERTIES
            LABELS executor TIMEOUT 90 SKIP_RETURN_CODE 77)
    endif()
endif()

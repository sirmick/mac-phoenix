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

include(${CMAKE_CURRENT_SOURCE_DIR}/CMakeRC.cmake)
cmrc_add_resource_library(resources
        WHENCE res
        res/System
        res/System.ad
        res/Browser
        res/Browser.ad
        res/Printer
        res/Printer.ad
        res/godata.sav
        res/printdef.ini
        res/printers.ini
        res/about/1_License.txt
        res/about/2_Credits.txt
        res/about/3_Tips.txt
        res/resource-policy.txt
        res/extension-policy.txt
    )

# --- CPU facade --------------------------------------------------------------
# syn68k_common.cpp implements the syn68k API on a cpu::Core
# (src/cpu/core), chosen at startup (--core): mame-68k.
add_library(executor_cpu STATIC
    cpu/syn68k_common.cpp
    cpu/PowerCore.cpp
)
target_include_directories(executor_cpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_include_directories(executor_cpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/romlib)   # base/trap_history.h
target_compile_options(executor_cpu PRIVATE ${EXECUTOR_COMMON_FLAGS})
target_link_libraries(executor_cpu PUBLIC cpu_core Threads::Threads)
phoenix_exempt_from_hardening(executor_cpu)

# --- Toolbox -------------------------------------------------------------------
add_subdirectory(romlib)
target_compile_options(romlib PRIVATE ${EXECUTOR_COMMON_FLAGS} -Wno-deprecated-declarations)
# --logtraps support (cheap when the flag is off).
target_compile_definitions(romlib PUBLIC EXECUTOR_ENABLE_LOGGING)
phoenix_exempt_from_hardening(romlib)
phoenix_exempt_from_hardening(front-end-phoenix)
target_compile_options(front-end-phoenix PRIVATE ${EXECUTOR_COMMON_FLAGS})

# Into the one binary (its g_platform comes from src/core).
target_sources(mac-phoenix PRIVATE host/executor_child.cpp)
target_link_libraries(mac-phoenix PRIVATE front-end-phoenix)
target_compile_definitions(mac-phoenix PRIVATE HAVE_EXECUTOR=1)
target_include_directories(mac-phoenix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/romlib/config/front-ends/phoenix)

# g_platform for the test binaries, which have no src/core. Compiled into
# each consumer (an archive would sit before the libraries that use it).
add_library(executor_platform_glue INTERFACE)
target_sources(executor_platform_glue INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cpu/platform_glue.cpp)
target_include_directories(executor_platform_glue INTERFACE ${PROJECT_SOURCE_DIR}/src/common/include)
target_link_libraries(executor_platform_glue INTERFACE executor_cpu cpu_core Threads::Threads)

# --- Tests ---------------------------------------------------------------------
if(BUILD_TESTS)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(GOOGLETEST_VERSION 1.9.0)
    add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)

    add_test(NAME executor.smoke.web_backend
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_web_backend.sh
                $<TARGET_FILE:mac-phoenix> ${CMAKE_CURRENT_BINARY_DIR}/smoke)
    set_tests_properties(executor.smoke.web_backend PROPERTIES
        LABELS executor TIMEOUT 90 SKIP_RETURN_CODE 77)
endif()

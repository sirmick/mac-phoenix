# =============================================================================
# Executor core (Executor 2000, MIT) on MacPhoenix's UAE 68k core.
# See docs/executor/PLAN.md.
#
#   executor_cpu          syn68k API facade over UAE + PowerCore stub
#   romlib                the imported Toolbox
#   front-end-phoenix     video/cursor front end + executor_host API,
#                         linked into mac-phoenix (`--backend executor`)
#   executor_tests        Executor's gtest suite, now on UAE
# =============================================================================

# Build tools and libraries Executor needs. Missing ones skip the Executor
# targets instead of breaking the whole mac-phoenix build.
find_package(Boost QUIET CONFIG COMPONENTS filesystem program_options)
find_package(Ruby 2.1 QUIET)
find_program(PERL_COMMAND perl)
find_program(BISON_COMMAND bison)
set(_executor_missing)
if(NOT Boost_FOUND)
    list(APPEND _executor_missing "Boost (filesystem, program_options)")
endif()
if(NOT RUBY_EXECUTABLE)
    list(APPEND _executor_missing "ruby")
endif()
if(NOT PERL_COMMAND)
    list(APPEND _executor_missing "perl")
endif()
if(NOT BISON_COMMAND)
    list(APPEND _executor_missing "bison")
endif()
if(_executor_missing)
    message(WARNING "Skipping mac-phoenix-executor; missing: ${_executor_missing}")
    return()
endif()
find_package(Threads REQUIRED)
add_compile_definitions(BOOST_ALL_NO_LIB)

# Guest RAM lives at host 0 and the guest dereferences low addresses as
# ordinary data. Never let the compiler treat those as UB.
set(EXECUTOR_COMMON_FLAGS -fno-delete-null-pointer-checks)

# Everything the guest can address must be below 4GB, which includes the
# binary's own data (callback page, trap tables, GUEST<> globals).
function(executor_link_low target)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        target_link_options(${target} PRIVATE
            "-T${PROJECT_SOURCE_DIR}/ldscripts/linux-x86_64.ld")
    else()
        message(WARNING "${target}: no low-address link layout for this platform")
    endif()
endfunction()

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
set(UAE_DIR ${PROJECT_SOURCE_DIR}/src/cpu/uae_cpu)
add_library(executor_cpu STATIC
    cpu/syn68k_uae.cpp
    cpu/PowerCore.cpp
)
target_include_directories(executor_cpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpu)
target_include_directories(executor_cpu PRIVATE
    ${UAE_DIR}
    ${UAE_DIR}/compiler
    ${PROJECT_SOURCE_DIR}/src/common/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/cpu/kpx/compat
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
    set(_exe_uae_jit 1)
else()
    set(_exe_uae_jit 0)
endif()
set(_exe_uae_flags
    -DCPU_EMU_SIZE=0 -DREAL_ADDRESSING=0 -DDIRECT_ADDRESSING=1
    -DUSE_JIT=${_exe_uae_jit} -DUSE_JIT_FPU=0 -DUSE_PREFETCH_BUFFER=0 -DFPU_IEEE
    -Wno-unused-parameter -Wno-sign-compare)
target_compile_options(executor_cpu PRIVATE ${_exe_uae_flags} ${EXECUTOR_COMMON_FLAGS})
target_link_libraries(executor_cpu PUBLIC Threads::Threads)
phoenix_exempt_from_hardening(executor_cpu)

# --- Toolbox -------------------------------------------------------------------
add_subdirectory(romlib)
target_compile_options(romlib PRIVATE ${EXECUTOR_COMMON_FLAGS} -Wno-deprecated-declarations)
# --logtraps support (cheap when the flag is off).
target_compile_definitions(romlib PUBLIC EXECUTOR_ENABLE_LOGGING)
phoenix_exempt_from_hardening(romlib)
phoenix_exempt_from_hardening(front-end-phoenix)
target_compile_options(front-end-phoenix PRIVATE ${EXECUTOR_COMMON_FLAGS})

# Into the one binary. mac-phoenix already links UAE (and gets its
# g_platform, CPUType, intlev, ... from src/core and src/cpu).
target_sources(mac-phoenix PRIVATE host/executor_child.cpp)
target_link_libraries(mac-phoenix PRIVATE front-end-phoenix)
target_compile_definitions(mac-phoenix PRIVATE HAVE_EXECUTOR=1)
target_include_directories(mac-phoenix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/romlib/config/front-ends/phoenix)

# Stand-ins for the mac-phoenix symbols UAE needs, for the test binary.
# Compiled into each consumer (an archive would sit before the UAE libs
# that reference it on the link line).
add_library(executor_uae_glue INTERFACE)
target_sources(executor_uae_glue INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cpu/uae_glue.cpp)
target_include_directories(executor_uae_glue INTERFACE ${PROJECT_SOURCE_DIR}/src/common/include)
target_link_libraries(executor_uae_glue INTERFACE
    -Wl,--whole-archive cpuemu_ff cpustbl_ff -Wl,--no-whole-archive
    -Wl,--start-group uae_cpu uae_jit vm_alloc executor_cpu -Wl,--end-group
    Qt6::Core Threads::Threads)

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

# =============================================================================
# The Windows build: Executor and its desktop application alone, cross-built
# from Linux (tools/windows/build.sh; toolchain tools/windows/llvm-mingw.cmake).
#
#   tools/windows/build.sh            # -> build-windows/executor-desktop.exe
#   tools/windows/package.sh          # -> executor-windows-VERSION.zip
#
# The same romlib, CPU facade and MAME cores as the native build, with the
# host edges' Windows sides (cpu/host/windows: VirtualAlloc). None of
# mac-phoenix itself (the web server, WebRTC, codecs, net-bridge), the ROM
# machines or UAE. No network yet (EXECUTOR_NET=none) and no Skia.
# =============================================================================

set(BUILD_EXECUTOR ON)
set(EXECUTOR_NET none CACHE STRING "Executor's network backend: posix | none | wasm")
option(BUILD_TESTS "Build test suite" OFF)

# No hardening flags here; the native build's helper is a no-op.
function(phoenix_exempt_from_hardening target)
endfunction()

# Static C++ runtime pieces so the exe needs only Qt's DLLs and the
# toolchain's libc++/libunwind (which Qt's own DLLs need anyway).
add_compile_options(-fexceptions)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES)

# The version (git describe) in the window title and the log's first line
execute_process(COMMAND git describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE EXECUTOR_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT EXECUTOR_VERSION)
    set(EXECUTOR_VERSION unknown)
endif()

find_package(Threads REQUIRED)

# Qt's Windows build names its own moc.exe & co.; the build machine's Qt of
# the same version runs instead (found here, before frontends/desktop does).
find_package(Qt6 6.4 COMPONENTS Core Gui Widgets)
set(QT_HOST_LIBEXEC "/usr/lib/qt6/libexec" CACHE PATH "The build machine's Qt 6 tools (moc, rcc, uic)")
foreach(_tool moc rcc uic)
    if(TARGET Qt6::${_tool})
        set_target_properties(Qt6::${_tool} PROPERTIES
            IMPORTED_LOCATION ${QT_HOST_LIBEXEC}/${_tool}
            IMPORTED_LOCATION_RELEASE ${QT_HOST_LIBEXEC}/${_tool}
            IMPORTED_LOCATION_RELWITHDEBINFO ${QT_HOST_LIBEXEC}/${_tool})
    endif()
endforeach()
add_subdirectory(src/cpu/mame)
add_subdirectory(src/cpu/core)
add_subdirectory(executor)

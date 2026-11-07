# CMake toolchain file: Windows x86-64 from Linux with llvm-mingw (clang,
# libc++, UCRT), against Qt's own llvm-mingw build of Qt 6 (aqtinstall's
# win64_llvm_mingw: the same compiler and C++ library, so Qt's C++ ABI
# matches). tools/windows/build.sh fetches both into ~/storage/deps/windows
# and configures with this file:
#
#   cmake -B build-windows -DCMAKE_TOOLCHAIN_FILE=tools/windows/llvm-mingw.cmake
#
# LLVM_MINGW and QT_WINDOWS override where they are; moc/rcc/uic come from
# the host's Qt of the same version (QT_HOST_PATH).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_deps "$ENV{HOME}/storage/deps/windows")
if(NOT LLVM_MINGW)
    set(LLVM_MINGW "${_deps}/llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64")
endif()
if(NOT QT_WINDOWS)
    set(QT_WINDOWS "${_deps}/qt/6.8.2/llvm-mingw_64")
endif()
# try_compile projects see these too
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW QT_WINDOWS QT_HOST_PATH)

set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   "${LLVM_MINGW}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW}/bin/${_triple}-windres")
set(CMAKE_AR           "${LLVM_MINGW}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${LLVM_MINGW}/bin/llvm-ranlib" CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW}/${_triple}" "${QT_WINDOWS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH "${QT_WINDOWS}")

# Qt's moc, rcc and uic run on the build machine: the host's Qt 6.8
if(NOT QT_HOST_PATH)
    set(QT_HOST_PATH "/usr" CACHE PATH "Host Qt (tools for the cross build)")
    set(Qt6HostInfo_DIR "/usr/lib/x86_64-linux-gnu/cmake/Qt6HostInfo" CACHE PATH "")
endif()

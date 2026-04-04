# Guest test suite - Cross-compiled with Retro68
#
# Prerequisites:
#   Retro68 toolchain installed, m68k-apple-macos-gcc on PATH
#
# Build:
#   cmake -B build-guest -DCMAKE_TOOLCHAIN_FILE=$RETRO68/m68k-apple-macos.toolchain.cmake
#   cmake --build build-guest
#
# Or directly:
#   m68k-apple-macos-gcc -o MacTestSuite src/main.c src/test_extfs.c \
#       src/test_audio.c src/test_serial.c src/test_network.c src/test_disk.c \
#       -I src

cmake_minimum_required(VERSION 3.9)
project(MacTestSuite LANGUAGES C)

add_application(MacTestSuite
    src/main.c
    src/test_extfs.c
    src/test_audio.c
    src/test_serial.c
    src/test_network.c
    src/test_disk.c
)

target_include_directories(MacTestSuite PRIVATE src)

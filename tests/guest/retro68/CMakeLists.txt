# RetroTest (rtest.c): built by build.sh with Retro68's toolchains, against
# the Multiversal Interfaces generated from Executor's definitions
# (MV_OUT: make-multiverse.rb -G CIncludes -o MV_OUT), not the toolchain's.
cmake_minimum_required(VERSION 3.16)
project(RetroTest C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    link_directories(BEFORE "${MV_OUT}/lib68k")
endif()
add_application(RetroTest rtest.c CREATOR "MPrt")
if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    # OSA's calls are AppleScriptLib's on PowerPC (OSA.yaml: library:)
    target_link_libraries(RetroTest AppleScriptLib)
endif()

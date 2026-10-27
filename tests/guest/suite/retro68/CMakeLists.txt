# MacPhoenixSuite built with Retro68 (build.sh), against the Multiversal
# Interfaces generated from Executor's definitions (MV_OUT: make-multiverse.rb
# -G CIncludes -o MV_OUT), not the toolchain's or Apple's: the same sources
# as the MPW build (../build.mpw), which uses Apple's.
cmake_minimum_required(VERSION 3.16)
project(MacPhoenixSuiteRetro68 C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
set(CMAKE_REZ_FLAGS "-I${MV_OUT}/RIncludes")
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    link_directories(BEFORE "${MV_OUT}/lib68k")
endif()
set(S "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_application(MacPhoenixSuite
    ${S}/main.c ${S}/files.c ${S}/audio.c ${S}/ascript.c ${S}/tcpip.c ${S}/net.c
    ${S}/netdeep.c ${S}/mactcp.c ${S}/keyboard.c ${S}/finder.c ${S}/size.r
    CREATOR "MPhS")
target_include_directories(MacPhoenixSuite PRIVATE ${S})
if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    # OSA's calls are AppleScriptLib's on PowerPC (OSA.yaml: library:)
    target_link_libraries(MacPhoenixSuite AppleScriptLib)
endif()

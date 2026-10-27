# CfgTest (CfgTest.c) built with Retro68 against the Multiversal Interfaces
# generated from Executor's definitions (MV_OUT: make-multiverse.rb -G
# CIncludes -o MV_OUT), by build-retro68.sh: CfgTest68K.bin and
# CfgTestPPC.bin. The configuration database's calls are reached through
# the Shared Library Manager by the glue in MV_OUT/lib68k
# (custom/OpenTransportGlue.c) on 68k, from OTConfigLib on PowerPC.
cmake_minimum_required(VERSION 3.16)
project(CfgTest C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    link_directories(BEFORE "${MV_OUT}/libppc")
else()
    link_directories(BEFORE "${MV_OUT}/lib68k")
endif()
add_application(CfgTest CfgTest.c CREATOR "OTcf")
if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    target_link_libraries(CfgTest OpenTransportApp OpenTransportLib)
else()
    target_link_libraries(CfgTest OpenTransportApp)
endif()

# OTTest (OTTest.c) built with Retro68 against the Multiversal Interfaces
# generated from Executor's definitions (MV_OUT: make-multiverse.rb -G
# CIncludes -o MV_OUT), by build-retro68.sh: OTTestRetroPPC.bin and
# OTTestRetro68K.bin. Open Transport's public calls are the glue in
# MV_OUT/libppc and MV_OUT/lib68k (custom/OpenTransportGlue.c): over the OT
# libraries' imports on PowerPC, through the Shared Library Manager on 68k.
cmake_minimum_required(VERSION 3.16)
project(OTTest C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    link_directories(BEFORE "${MV_OUT}/libppc")
else()
    link_directories(BEFORE "${MV_OUT}/lib68k")
endif()
add_application(OTTest OTTest.c CREATOR "OTtR")
if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    target_link_libraries(OTTest OpenTransportApp OpenTransportLib OpenTptInternetLib)
else()
    target_link_libraries(OTTest OpenTransportApp)
endif()

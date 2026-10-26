# OTTest (OTTest.c) as a PowerPC application built with Retro68 against the
# Multiversal Interfaces generated from Executor's definitions (MV_OUT:
# make-multiverse.rb -G CIncludes -o MV_OUT): OTTestRetroPPC.bin, by
# build-retro68.sh. Open Transport's public calls are the glue in
# MV_OUT/libppc (custom/OpenTransportGlue.c) over the OT libraries' imports.
cmake_minimum_required(VERSION 3.16)
project(OTTest C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
link_directories(BEFORE "${MV_OUT}/libppc")
add_application(OTTest OTTest.c CREATOR "OTtR")
target_link_libraries(OTTest OpenTransportApp OpenTransportLib OpenTptInternetLib)

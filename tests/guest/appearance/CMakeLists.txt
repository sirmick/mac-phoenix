# ApprProbe (ApprProbe.c) built with Retro68 for PowerPC against the
# Multiversal Interfaces generated from Executor's definitions (MV_OUT), by
# build-retro68.sh: ApprProbePPC.bin.
cmake_minimum_required(VERSION 3.16)
project(ApprProbe C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
link_directories(BEFORE "${MV_OUT}/libppc")
add_application(ApprProbe ApprProbe.c CREATOR "ApPr")
target_link_libraries(ApprProbe AppearanceLib)

# StrProbe (StrProbe.c) built with Retro68 for 68k against the
# Multiversal Interfaces generated from Executor's definitions (MV_OUT), by
# build-retro68.sh: StrProbe.bin.
cmake_minimum_required(VERSION 3.16)
project(StrProbe C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
add_application(StrProbe StrProbe.c StrProbe.r CREATOR "StPb")

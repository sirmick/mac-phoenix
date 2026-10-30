# PartProbe (PartProbe.c) built with Retro68 for 68k against the
# Multiversal Interfaces generated from Executor's definitions (MV_OUT), by
# build-retro68.sh: PartProbe.bin.
cmake_minimum_required(VERSION 3.16)
project(PartProbe C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
add_application(PartProbe PartProbe.c PartProbe.r CREATOR "PrPb")

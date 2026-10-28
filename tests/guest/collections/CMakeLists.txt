# CollectionTest (CollectionTest.c) built with Retro68 for PowerPC against the
# Multiversal Interfaces generated from Executor's definitions (MV_OUT), by
# build-retro68.sh: CollectionTestPPC.bin.
cmake_minimum_required(VERSION 3.16)
project(CollectionTest C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
link_directories(BEFORE "${MV_OUT}/libppc")
add_application(CollectionTest CollectionTest.c CREATOR "CoTs")
target_link_libraries(CollectionTest CollectionsLib)

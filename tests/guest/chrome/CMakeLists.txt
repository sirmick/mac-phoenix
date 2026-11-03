# ChromeGallery (ChromeGallery.c) built with Retro68 for PowerPC against the
# Multiversal Interfaces generated from Executor's definitions (MV_OUT), by
# build-retro68.sh: ChromeGalleryPPC.bin.
cmake_minimum_required(VERSION 3.16)
project(ChromeGallery C)
set(MV_OUT "" CACHE PATH "make-multiverse.rb -G CIncludes output")
include_directories(BEFORE "${MV_OUT}/CIncludes")
link_directories(BEFORE "${MV_OUT}/libppc")
add_application(ChromeGallery ChromeGallery.c ChromeGallery.r CREATOR "ChGa")
target_link_libraries(ChromeGallery AppearanceLib ControlsLib WindowsLib)

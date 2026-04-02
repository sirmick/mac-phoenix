# Core platform-independent managers

add_library(core STATIC
  adb.cpp
  audio.cpp
  cdrom.cpp
  cpu_context.cpp
  ppc_subprocess.cpp
  disk.cpp
  boot_progress.cpp
  command_bridge.cpp
  emul_op.cpp
  emulator_init.cpp
  ether.cpp
  extfs.cpp
  macos_util.cpp
  main.cpp
  rom_patches.cpp
  rsrc_patches.cpp
  scsi.cpp
  serial.cpp
  slot_rom.cpp
  sony.cpp
  timer.cpp
  timer_unix.cpp
  user_strings.cpp
  video.cpp
  xpram.cpp
  ../common/platform.cpp
  ../common/crash_handler_init.cpp
  ../common/sigsegv.cpp
)

target_include_directories(core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../config
  ${CMAKE_CURRENT_SOURCE_DIR}/../cpu
  ${CMAKE_CURRENT_SOURCE_DIR}/../cpu/uae_cpu
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/../ipc
)

target_link_libraries(core PRIVATE
  Threads::Threads
  nlohmann_json::nlohmann_json
)

target_compile_options(core PRIVATE
  -DHAVE_SIGINFO_T
  -DHAVE_SIGACTION
  -DHAVE_SIGSEGV_SKIP_INSTRUCTION
  -DHAVE_UNISTD_H
  -Wno-sign-compare
  -O2
)

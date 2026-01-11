# Core platform-independent managers
core_sources = [
  'adb.cpp',
  'audio.cpp',
  'cdrom.cpp',
  'cpu_context.cpp',  # Phase 2: Self-contained CPU context
  'disk.cpp',
  'emul_op.cpp',
  'emulator_init.cpp',  # Deferred initialization support
  'ether.cpp',
  'extfs.cpp',
  'json_config.cpp',
  'macos_util.cpp',
  'main.cpp',
  'prefs.cpp',
  'prefs_items.cpp',
  'rom_patches.cpp',
  'rom_patches_unicorn.cpp',
  'rom_patches_aline.cpp',
  'rsrc_patches.cpp',
  'scsi.cpp',
  'serial.cpp',
  'slot_rom.cpp',
  'sony.cpp',
  'timer.cpp',
  'user_strings.cpp',
  'video.cpp',
  'xpram.cpp',
  '../common/platform.cpp',  # Platform initialization
  '../common/crash_handler_init.cpp',  # Crash handler with stack traces
]

core_inc = include_directories(
  '.',
  '../common/include',
  '../config',  # For emulator_config.h
  '../cpu',
  '../cpu/uae_cpu',
  '../drivers/platform',  # For timer_interrupt.h
  '../../external/json/include',  # nlohmann/json
)

core_lib = static_library('core',
  core_sources,
  include_directories: core_inc,
  dependencies: [threads_dep],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: core_inc,
)

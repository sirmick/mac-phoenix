# Core platform-independent managers
core_sources = [
  'adb.cpp',
  'audio.cpp',
  'cdrom.cpp',
  'cpu_context.cpp',  # Phase 2: Self-contained CPU context
  'cpu_process.cpp',  # Fork-based CPU process management
  'disk.cpp',
  'boot_progress.cpp',
  'command_bridge.cpp',
  'emul_op.cpp',
  'emul_op_ppc.cpp',  # PPC SHEEP opcode dispatch
  'emulator_init.cpp',  # Deferred initialization support
  'ether.cpp',
  'extfs.cpp',
  'macos_util.cpp',
  'main.cpp',
  'rom_patches.cpp',
  'rom_patches_ppc.cpp',
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
  '../common/sigsegv.cpp',  # SIGSEGV handler for skipping illegal memory accesses
]

core_inc = include_directories(
  '.',
  '../common/include',
  '../config',  # For emulator_config.h
  '../cpu',
  '../cpu/uae_cpu',
  '../drivers/platform',  # For timer_interrupt.h
)

core_lib = static_library('core',
  core_sources,
  include_directories: core_inc,
  dependencies: [threads_dep, nlohmann_json_dep],
  cpp_args: ['-DHAVE_SIGINFO_T', '-DHAVE_SIGACTION', '-DHAVE_SIGSEGV_SKIP_INSTRUCTION', '-DHAVE_UNISTD_H',
             '-Wno-sign-compare'],  # sigsegv.cpp legacy code
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: core_inc,
)

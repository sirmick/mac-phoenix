# KPX (Kheperix) PPC CPU with dyngen JIT + bridge + boot
#
# Near-verbatim from legacy SheepShaver, with dyngen JIT enabled.
# Includes the KPX interpreter/JIT core, Platform API bridge, and PPC boot code.
#
# Split into two groups: interpreter core (needs -O2 for performance)
# and dyngen files (must use -O0 due to precompiled machine code ICE at -O2).

kpx_interpreter_sources = [
  'src/cpu/ppc/ppc-cpu.cpp',
  'src/cpu/ppc/ppc-execute.cpp',
  'src/cpu/ppc/ppc-decode.cpp',
  'src/cpu/ppc/ppc-translate.cpp',    # PPC block compiler (compile_block)
  'src/utils/utils-cpuinfo.cpp',      # CPU feature detection
  'src/mathlib/mathlib.cpp',
  'src/mathlib/ieeefp.cpp',
  'cpu_ppc_kpx.cpp',             # Platform API bridge
  'rom_patches_ppc.cpp',         # PPC ROM patching (from SheepShaver)
  'emul_op_ppc.cpp',             # PPC EMUL_OP dispatch (from SheepShaver)
  'rsrc_patches_ppc.cpp',        # Runtime resource patches (from SheepShaver)
  'ppc_memory.cpp',              # SheepMem, Microseconds, ExtFS bridge               # Stub implementations for subsystem functions
  'thunks_ppc.cpp',              # Native op thunks (from SheepShaver)
  'init_ppc.cpp',                # InitAll/ExitAll (from SheepShaver main.cpp)
  'video_ppc.cpp',               # PPC video driver (from SheepShaver video.cpp + WebRTC)
  'gfxaccel_ppc.cpp',            # Native QuickDraw acceleration (from SheepShaver gfxaccel.cpp)
  'macos_util_ppc.cpp',          # Mac OS utility functions (FindLibSymbol etc.)
  'name_registry_ppc.cpp',
  'serial_ppc.cpp',                # PPC serial driver (IOCommandIsComplete, from SheepShaver)
  'compat/vm_alloc.cpp',
]

kpx_dyngen_sources = [
  'src/cpu/ppc/ppc-jit.cpp',          # PPC JIT compiler
  'src/cpu/ppc/ppc-dyngen.cpp',       # PPC dyngen code generator
  'src/cpu/jit/jit-cache.cpp',        # JIT code cache
  'src/cpu/jit/basic-dyngen.cpp',     # Basic dyngen framework
  'src/cpu/jit/cxxdemangle.cpp',      # C++ name demangling
]

kpx_inc = [
  include_directories('compat'),     # sysdeps.h, debug.h, vm_alloc.h, SheepShaver compat headers
  include_directories('include'),    # basic-cpu.hpp, nvmemfun.hpp, block-alloc.hpp, etc.
  include_directories('src'),        # cpu/vm.hpp, cpu/ppc/*.hpp, mathlib/*.hpp
  include_directories('dyngen_precompiled'),  # precompiled dyngen ops headers
]

kpx_common_inc = kpx_inc + [
  include_directories('../../common/include'),  # platform.h, driver headers
  include_directories('../../drivers/video'),    # video_output.h, video_encoder_thread.h
  include_directories('../../config'),           # emulator_config.h
  include_directories('../../core'),             # boot_progress.h
]

kpx_cpp_args = [
  '-DSHEEPSHAVER=1',
  '-DEMULATED_PPC=1',
  '-DPPC_ENABLE_JIT=1',
  '-DPPC_REENTRANT_JIT=1',
  '-DPPC_CHECK_INTERRUPTS=0',  # Must be 0 to match legacy — with =1, trigger_interrupt() sets spcflags which changes interrupt timing
  '-DPPC_DECODE_CACHE=1',
  '-DPPC_FLIGHT_RECORDER=1',  # Must be 1 — matches legacy sysdeps.h, dyngen ops struct layout
  '-DREAL_ADDRESSING=1',
  '-DKPX_MAX_CPUS=1',
  '-DENABLE_DYNGEN=1',
  '-DENABLE_MON=0',
  '-DUSE_ETHER_FULL_DRIVER=1',  # Must match legacy for deterministic name registry
  '-DHAVE_SIGSEGV_SKIP_INSTRUCTION=1',
  '-DHAVE_LINKER_SCRIPT=1',  # Sets MAP_BASE=0x10000000 in vm_alloc.cpp (matches legacy)
  '-D_GNU_SOURCE',
  '-DHAVE_CONFIG_H',
  '-DHAVE_FCNTL_H',
  '-w',  # Suppress warnings in legacy code
  '-DPPC_DETERMINISTIC_REPLAY=1',  # Disable JIT, use interpreter with replay interrupts
]

# Interpreter core at -O2 (hot loop needs optimization for acceptable speed)
kpx_interp_lib = static_library('kpx_interp',
  kpx_interpreter_sources,
  include_directories: kpx_common_inc,
  cpp_args: kpx_cpp_args + ['-O1'],
)

# Dyngen/JIT at -O0 (precompiled machine code in headers causes ICE at -O2)
kpx_dyngen_lib = static_library('kpx_dyngen',
  kpx_dyngen_sources,
  include_directories: kpx_common_inc,
  cpp_args: kpx_cpp_args,
)

kpx_dep = declare_dependency(
  link_with: [kpx_interp_lib, kpx_dyngen_lib],
  include_directories: kpx_inc,
)

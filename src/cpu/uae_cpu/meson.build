# UAE CPU Core Build
#
# Minimal build of UAE M68K emulation for dual-CPU validation
#
# IMPORTANT: Like BasiliskII, we compile cpuemu.cpp AND cpustbl.cpp TWICE:
# 1. Without -DNOFLAGS to get op_xxx_ff functions and op_smalltbl_X_ff tables
# 2. With -DNOFLAGS to get op_xxx_nf functions and op_smalltbl_X_nf tables
#
# Both versions are linked together. The _ff tables are used by build_cpufunctbl(),
# and the _nf functions are used at runtime for better performance.

uae_inc = include_directories('.')
common_inc = include_directories('../../common/include')
timer_inc = include_directories('../../drivers/platform')  # For timer_interrupt.h
compiler_inc = include_directories('compiler')  # For comptbl.h
kpx_compat_inc = include_directories('../kpx/compat')  # For vm_alloc.h

# Common compiler args for UAE CPU
uae_common_args = [
  '-DCPU_EMU_SIZE=0',
  '-DREAL_ADDRESSING=0',
  '-DDIRECT_ADDRESSING=1',
  '-DUSE_JIT=1',
  '-DUSE_JIT_FPU=0',
  '-DUSE_PREFETCH_BUFFER=0',
  '-DFPU_IEEE',
  '-Wno-unused-variable',
  '-Wno-unused-parameter',
  '-Wno-unused-function',
  '-Wno-sign-compare',
  '-Wno-unused-but-set-variable',
  '-Wno-parentheses',
  '-Wno-format',
  '-Wno-narrowing',
  '-Wno-unused-label',
  '-Wno-shift-negative-value',
]

# Compile cpuemu.cpp WITHOUT -DNOFLAGS to get op_xxx_ff functions
cpuemu_ff_obj = static_library('cpuemu_ff',
  'cpuemu_ff.cpp',
  include_directories: [uae_inc, common_inc, compiler_inc, include_directories('../..')],
  cpp_args: uae_common_args,  # No -DNOFLAGS
)

# Compile cpustbl.cpp WITHOUT -DNOFLAGS to get op_smalltbl_X_ff tables
cpustbl_ff_obj = static_library('cpustbl_ff',
  'cpustbl.cpp',
  include_directories: [uae_inc, common_inc, compiler_inc, include_directories('../..')],
  cpp_args: uae_common_args,  # No -DNOFLAGS
)

# vm_alloc: virtual memory allocation (needed by JIT for translation cache)
vm_alloc_lib = static_library('vm_alloc',
  '../kpx/compat/vm_alloc.cpp',
  include_directories: [kpx_compat_inc],
  cpp_args: [
    '-DHAVE_MMAP_VM=1',
    '-DHAVE_MMAP_ANONYMOUS=1',
    '-DHAVE_UNISTD_H=1',
    '-DHAVE_SYS_MMAN_H=1',
    '-DHAVE_FCNTL_H=1',
    '-Wno-unused-parameter',
  ],
)

# JIT compiler sources (x86/x86_64 dynamic recompiler)
# compemu_support.cpp #includes codegen_x86.cpp directly
jit_args = uae_common_args + [
  '-fpermissive',         # Legacy C++ enum conversions
  '-Wno-missing-field-initializers',
]

jit_lib = static_library('uae_jit',
  'compiler/compemu_support.cpp',
  'compiler/compemu_fpp.cpp',
  'compiler/compemu.cpp',
  'compiler/compstbl.cpp',
  include_directories: [uae_inc, common_inc, compiler_inc, kpx_compat_inc, include_directories('../..')],
  cpp_args: jit_args,
)

# Regular UAE sources compiled WITH -DNOFLAGS
uae_sources = [
  'newcpu.cpp',
  'cpuemu_nf.cpp',   # No-flags version (JIT disabled)
  'cpustbl_nf.cpp',  # No-flags version (creates op_smalltbl_X_nf tables)
  'cpudefs.cpp',     # Generated CPU instruction definitions
  'readcpu.cpp',
  'memory.cpp',
  'basilisk_glue.cpp',
  'basilisk_stubs.cpp',  # Temporary stubs for missing BasiliskII functions
  'fpu/fpu_ieee.cpp',    # IEEE FPU emulation (MC68881/68040)
  # NOTE: cpuemu_ff_aliases.cpp is NOT needed - real _ff functions come from cpuemu_ff library
]

uae_lib = static_library('uae_cpu',
  uae_sources,
  include_directories: [uae_inc, common_inc, compiler_inc, kpx_compat_inc, timer_inc, include_directories('../..')],
  cpp_args: uae_common_args + ['-DNOFLAGS'],  # Add -DNOFLAGS
)

# Dependency that includes _nf library, _ff objects, and JIT compiler
uae_dep = declare_dependency(
  link_with: [uae_lib, jit_lib, vm_alloc_lib],
  link_whole: [cpuemu_ff_obj, cpustbl_ff_obj],  # Force inclusion of _ff objects
  include_directories: uae_inc
)

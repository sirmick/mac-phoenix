# Kheperix (KPX) PowerPC CPU interpreter
# Extracted from legacy/SheepShaver/src/kpx_cpu/
# Interpreter-only build (no JIT/dyngen)

kpx_sources = [
  'cpu/ppc/ppc-cpu.cpp',
  'cpu/ppc/ppc-decode.cpp',
  'cpu/ppc/ppc-execute.cpp',
  'cpu/ppc/ppc-translate.cpp',
  'mathlib/mathlib.cpp',
  'mathlib/ieeefp.cpp',
  'utils/utils-cpuinfo.cpp',
]

kpx_inc = include_directories(
  '.',           # for cpu/vm.hpp, cpu/ppc/*.hpp, utils/*.hpp, mathlib/*.hpp
  'include',     # for basic-cpu.hpp, nvmemfun.hpp, block-alloc.hpp, vm_alloc.h
)

kpx_lib = static_library('kpx_cpu',
  kpx_sources,
  include_directories: [
    kpx_inc,
    include_directories('../../common/include'),  # sysdeps.h, config.h
  ],
  cpp_args: [
    '-DSHEEPSHAVER',          # SheepShaver integration mode (no basic_cpu inheritance)
    '-DREAL_ADDRESSING=1',    # PPC uses direct host pointers for memory access
    '-DENABLE_DYNGEN=0',      # No JIT
    '-DKPX_MAX_CPUS=1',      # Single CPU
    '-DENABLE_MON=0',         # No cxmon debugger
    '-DHAVE_LRINT',           # glibc has lrint
    '-DHAVE_ROUND',           # glibc has round
    '-DHAVE_ROUNDF',          # glibc has roundf
    '-DHAVE_CEILF',           # glibc has ceilf
    '-DHAVE_FLOORF',          # glibc has floorf
    '-DHAVE_EXP2',            # glibc has exp2
    '-DHAVE_LOG2',            # glibc has log2
    '-DHAVE_TRUNCF',          # glibc has truncf
    '-DHAVE_FENV_H',          # glibc has fenv.h
    '-DSIZEOF_LONG_DOUBLE=16',
    '-DSIZEOF_DOUBLE=8',
    '-DSIZEOF_FLOAT=4',
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-missing-field-initializers',
    '-Wno-deprecated',        # std::unary_function/binary_function in nvmemfun.hpp
  ],
  override_options: ['warning_level=0'],
)

kpx_dep = declare_dependency(
  link_with: kpx_lib,
  include_directories: kpx_inc,
  compile_args: [
    '-DSHEEPSHAVER',
    '-DENABLE_DYNGEN=0',
    '-DKPX_MAX_CPUS=1',
  ],
)

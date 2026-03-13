# Always compile ALL CPU backends (runtime selection via platform API)
cpu_sources = [
  'cpu_uae.c',                # UAE backend for platform API
  'cpu_unicorn.cpp',          # Unicorn backend for platform API (C++ for EmulOp linkage)
  'cpu_dualcpu.c',            # DualCPU backend for platform API
  'cpu_trace.c',              # CPU execution tracing infrastructure
  'uae_wrapper.cpp',          # UAE CPU wrapper
  'unicorn_wrapper.c',        # Unicorn wrapper
  'unicorn_exec_loop.c',      # Phase 2: QEMU-style execution loop for Unicorn
  'unicorn_exception.c',      # Exception simulation for Unicorn
  'unicorn_validation.cpp',   # Validation module (used by dualcpu)
  'emulop_c_wrapper.cpp',     # C wrapper for EmulOp function
  'cpu_ppc_unicorn.cpp',      # PPC Unicorn backend (QEMU TCG JIT)
  'cpu_ppc_stubs.c',          # PPC backend stubs: KPX, DualCPU (not yet implemented)
]

# Build UAE CPU (always)
subdir('uae_cpu')

cpu_deps = [unicorn_dep, uae_dep]

# Build CPU library
cpu_lib = static_library('cpu',
  cpu_sources,
  dependencies: cpu_deps,
  include_directories: [
    include_directories('.'),
    include_directories('../common/include'),
    include_directories('../core'),  # For emul_op_ppc.h
    include_directories('../drivers/platform'),
  ]
)

cpu_dep = declare_dependency(
  link_with: cpu_lib,
  include_directories: include_directories('.')
)

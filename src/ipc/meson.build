# IPC subsystem for subprocess mode (PPC)
ipc_sources = [
  'video_ipc_ppc.cpp',
  'control_ipc_child.cpp',
  'ipc_client.cpp',
]

ipc_inc = include_directories('.')

ipc_lib = static_library('ipc',
  ipc_sources,
  include_directories: [ipc_inc, include_directories('../common/include')],
  dependencies: [threads_dep],
  cpp_args: ['-Wno-sign-compare', '-Wno-class-memaccess'],
)

ipc_dep = declare_dependency(
  link_with: ipc_lib,
  include_directories: ipc_inc,
)

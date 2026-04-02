project('sheepshaver-standalone', 'c', 'cpp',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

# Dependencies
threads_dep = dependency('threads')
m_dep = cc.find_library('m')
sdl2_dep = dependency('sdl2')

# Mac-phoenix source root (for pulling in UAE, platform API, etc.)
mp_src = '/home/mick/mac-phoenix-ppc/src'

# Include directories — legacy SS headers
ss_inc = include_directories(
  'src/kpx_cpu/include',
  'src/kpx_cpu/src',
  'src/include',
  'src/Unix',
  'src/CrossPlatform',
  'src/slirp',
  'src/IPC',
)

# Defines
common_args = [
  '-DUSE_JIT',
  '-DPPC_FLIGHT_RECORDER=1',
  '-DHAVE_CONFIG_H',
  '-D_REENTRANT',
  '-D_GNU_SOURCE',
  '-DDATADIR="/usr/local/share/SheepShaver"',
  '-DNO_SLIRP',
]

# ============================================================================
# Legacy SheepShaver sources
# ============================================================================

main_srcs = files(
  'src/main.cpp',
  'src/Unix/main_unix.cpp',
  'src/prefs.cpp',
  'src/prefs_items.cpp',
  'src/Unix/prefs_unix.cpp',
  'src/Unix/sys_unix.cpp',
  'src/rom_patches.cpp',
  'src/rsrc_patches.cpp',
  'src/emul_op.cpp',
  'src/name_registry.cpp',
  'src/macos_util.cpp',
  'src/timer.cpp',
  'src/Unix/timer_unix.cpp',
  'src/xpram.cpp',
  'src/Unix/xpram_unix.cpp',
  'src/adb.cpp',
  'src/sony.cpp',
  'src/disk.cpp',
  'src/cdrom.cpp',
  'src/scsi.cpp',
  'src/gfxaccel.cpp',
  'src/video.cpp',
  'src/audio.cpp',
  'src/ether.cpp',
  'src/thunks.cpp',
  'src/serial.cpp',
  'src/extfs.cpp',
  'src/Unix/disk_sparsebundle.cpp',
  'src/Unix/tinyxml2.cpp',
  'src/Unix/about_window_unix.cpp',
  'src/user_strings.cpp',
  'src/Unix/user_strings_unix.cpp',
  'src/Unix/rpc_unix.cpp',
  'src/Unix/sshpty.c',
  'src/Unix/strlcpy.c',
  'src/platform_stub.cpp',
)

xplat_srcs = files(
  'src/CrossPlatform/vm_alloc.cpp',
  'src/CrossPlatform/sigsegv.cpp',
  'src/CrossPlatform/video_blit.cpp',
)

sys_srcs = files(
  'src/SDL/video_sdl.cpp',
  'src/SDL/video_sdl2.cpp',
  'src/SDL/video_sdl3.cpp',
  'src/Unix/extfs_unix.cpp',
  'src/dummy/prefs_dummy.cpp',
  'src/Unix/serial_unix.cpp',
  'src/Unix/ether_unix.cpp',
  'src/Unix/Linux/scsi_linux.cpp',
  'src/SDL/audio_sdl.cpp',
  'src/SDL/audio_sdl3.cpp',
  'src/dummy/prefs_editor_dummy.cpp',
  'src/dummy/clip_dummy.cpp',
)

cpu_srcs = files(
  'src/kpx_cpu/src/cpu/jit/jit-cache.cpp',
  'src/kpx_cpu/src/cpu/jit/basic-dyngen.cpp',
  'src/kpx_cpu/src/cpu/ppc/ppc-dyngen.cpp',
  'src/kpx_cpu/src/cpu/ppc/ppc-jit.cpp',
  'src/kpx_cpu/src/mathlib/ieeefp.cpp',
  'src/kpx_cpu/src/mathlib/mathlib.cpp',
  'src/kpx_cpu/src/cpu/ppc/ppc-cpu.cpp',
  'src/kpx_cpu/src/cpu/ppc/ppc-decode.cpp',
  'src/kpx_cpu/src/cpu/ppc/ppc-execute.cpp',
  'src/kpx_cpu/src/cpu/ppc/ppc-translate.cpp',
  'src/kpx_cpu/src/utils/utils-cpuinfo.cpp',
  'src/kpx_cpu/sheepshaver_glue.cpp',
  'src/kpx_cpu/ppc-dis.c',
)

ss_srcs = main_srcs + xplat_srcs + sys_srcs + cpu_srcs

# ============================================================================
# UAE 68k CPU — compiled from mac-phoenix source
# ============================================================================

uae_inc = include_directories(
  mp_src / 'cpu/uae_cpu',
  mp_src / 'common/include',
  mp_src,
)

uae_args = [
  '-DCPU_EMU_SIZE=0',
  '-DREAL_ADDRESSING=0',
  '-DDIRECT_ADDRESSING=1',
  '-DUSE_JIT=0',
  '-DUSE_JIT_FPU=0',
  '-DUSE_PREFETCH_BUFFER=0',
  '-DFPU_IEEE',
  '-DHAVE_CONFIG_H',
  '-D_GNU_SOURCE',
  '-w',  # suppress warnings in UAE legacy code
]

uae_sources = []
foreach f : ['newcpu.cpp', 'cpuemu_nf.cpp', 'cpustbl_nf.cpp', 'cpudefs.cpp',
             'readcpu.cpp', 'memory.cpp', 'basilisk_glue.cpp', 'basilisk_stubs.cpp',
             'fpu/fpu_ieee.cpp']
  uae_sources += files(mp_src / 'cpu/uae_cpu' / f)
endforeach

uae_ff_sources = []
foreach f : ['cpuemu_ff.cpp', 'cpustbl.cpp']
  uae_ff_sources += files(mp_src / 'cpu/uae_cpu' / f)
endforeach

# UAE needs these stubs for mac-phoenix infrastructure it references
uae_stub_srcs = files('src/uae_stubs.cpp')

uae_lib = static_library('uae_cpu',
  uae_sources + uae_stub_srcs,
  include_directories: [uae_inc, include_directories(mp_src / 'drivers/platform')],
  cpp_args: uae_args + ['-DNOFLAGS'],
)

uae_ff_lib = static_library('uae_cpu_ff',
  uae_ff_sources,
  include_directories: [uae_inc],
  cpp_args: uae_args,  # no -DNOFLAGS
)

# ============================================================================
# Link everything
# ============================================================================

ldscript = meson.current_source_dir() / 'src/Unix/ldscripts/linux-x86_64.ld'

executable('SheepShaver',
  ss_srcs,
  include_directories: ss_inc,
  c_args: common_args,
  cpp_args: common_args,
  link_args: [
    '-Wl,-T,' + ldscript,
    '-Wl,--warn-common',
  ],
  link_with: [uae_lib],
  link_whole: [uae_ff_lib],
  dependencies: [threads_dep, m_dep, sdl2_dep],
)

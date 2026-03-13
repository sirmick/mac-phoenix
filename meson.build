project('mac-phoenix', ['c', 'cpp'],
  version: '2.0.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=debugoptimized'
  ]
)

# Options
build_basilisk = get_option('basilisk')
build_sheepshaver = get_option('sheepshaver')

# Dependencies
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

# Unicorn dependency (always required - all CPUs compiled)
# Built from submodule with patches applied
unicorn_inc = include_directories('subprojects/unicorn/include')
unicorn_lib = cc.find_library('unicorn',
  dirs: [meson.current_source_dir() / 'subprojects/unicorn/build'],
  static: true,
  required: true
)
unicorn_dep = declare_dependency(
  include_directories: unicorn_inc,
  dependencies: [unicorn_lib]
)

# Other dependencies
threads_dep = dependency('threads', required: true)
m_dep = cc.find_library('m', required: true)  # Math library for Unicorn

# WebRTC streaming dependencies (always enabled)
# JSON configuration (nlohmann/json from submodule)
nlohmann_json_dep = dependency('nlohmann_json', required: false, fallback: ['nlohmann_json', 'nlohmann_json_dep'])
if not nlohmann_json_dep.found()
  nlohmann_json_inc = include_directories('subprojects/nlohmann_json/include')
  nlohmann_json_dep = declare_dependency(include_directories: nlohmann_json_inc)
endif

# Video encoders
openh264_dep = dependency('openh264', required: false)
libvpx_dep = dependency('vpx', required: false)
libwebp_dep = dependency('libwebp', required: false)

# Audio encoder
opus_dep = dependency('opus', required: false)

# HTTP server
cpp_httplib_dep = dependency('cpp-httplib', required: false)

# OpenSSL (for MD5 in file_scanner.cpp and libdatachannel)
openssl_dep = dependency('openssl', required: true)

# WebRTC library (libdatachannel) - Build from submodule using CMake
cmake = import('cmake')
libdatachannel_opts = cmake.subproject_options()
libdatachannel_opts.add_cmake_defines({
  'NO_EXAMPLES': 'ON',
  'NO_TESTS': 'ON',
  'NO_WEBSOCKET': 'OFF',  # We need WebSocket server for signaling
  'NO_MEDIA': 'OFF',      # We need media transport for video/audio
  'BUILD_SHARED_LIBS': 'OFF',  # Static linking for simplicity
  'USE_NICE': 'OFF',      # Use built-in libjuice
})

libdatachannel_proj = cmake.subproject('libdatachannel', options: libdatachannel_opts)

# Use datachannel-static and manually add the sub-libraries
# (Meson's CMake integration doesn't properly handle transitive static libs)
libdatachannel_static_dep = libdatachannel_proj.dependency('datachannel-static')

# Manually link all the required static libraries
build_dir = meson.current_build_dir()
libdc_dir = build_dir / 'subprojects/libdatachannel'

libdatachannel_dep = declare_dependency(
  dependencies: [libdatachannel_static_dep],
  link_args: [
    '-L' + libdc_dir,
    '-ldatachannel_static',
    '-ljuice',
    '-lsrtp2',
    '-lusrsctp',
  ]
)

# libyuv for color conversion (Ubuntu package lacks .pc file, use find_library)
libyuv_lib = cc.find_library('yuv', required: false)
if libyuv_lib.found()
  libyuv_dep = declare_dependency(
    dependencies: [libyuv_lib],
    include_directories: include_directories('/usr/include')
  )
else
  libyuv_dep = dependency('', required: false)  # Dummy for .found() check
endif

message('WebRTC dependencies:')
message('  nlohmann_json: ' + nlohmann_json_dep.found().to_string())
message('  openh264: ' + openh264_dep.found().to_string())
message('  libvpx (VP9): ' + libvpx_dep.found().to_string())
message('  libwebp: ' + libwebp_dep.found().to_string())
message('  opus: ' + opus_dep.found().to_string())
message('  cpp-httplib: ' + cpp_httplib_dep.found().to_string())
message('  libdatachannel: ' + libdatachannel_dep.found().to_string())
message('  libyuv: ' + libyuv_dep.found().to_string())

# Build info
message('Build configuration:')
message('  All CPU backends compiled (runtime selection via --backend flag)')
message('  Build BasiliskII: ' + build_basilisk.to_string())
message('  Build SheepShaver: ' + build_sheepshaver.to_string())

# Subdirectories
subdir('src/cpu/kpx')  # KPX PPC interpreter (must be before src/cpu)
subdir('src/cpu')
subdir('src/core')
subdir('src/drivers')

# WebRTC streaming (always enabled)
subdir('src/config')
subdir('src/webserver')
subdir('src/webrtc')
subdir('src/drivers/video/encoders')
subdir('src/drivers/audio/encoders')

# Main executable
macemu_next_sources = ['src/main.cpp']
macemu_next_link_with = [core_lib, driver_lib, cpu_lib]
macemu_next_deps = [unicorn_dep, threads_dep, m_dep]
macemu_next_inc = [
  include_directories('src/common/include'),
  include_directories('src/core'),
  include_directories('src/cpu'),
  include_directories('src/cpu/uae_cpu'),
  include_directories('src/drivers'),
  include_directories('src/drivers/platform'),
]
macemu_next_cpp_args = []

# Add WebRTC support (always enabled)
macemu_next_link_with += [config_lib, webserver_lib, webrtc_lib, video_encoder_lib, audio_encoder_lib]
macemu_next_deps += [nlohmann_json_dep, libdatachannel_dep]

# Add codec dependencies if found
if openh264_dep.found()
  macemu_next_deps += [openh264_dep]
endif
if libvpx_dep.found()
  macemu_next_deps += [libvpx_dep]
endif
if libwebp_dep.found()
  macemu_next_deps += [libwebp_dep]
endif
if opus_dep.found()
  macemu_next_deps += [opus_dep]
endif
if libyuv_dep.found()
  macemu_next_deps += [libyuv_dep]
endif

# Add OpenSSL (required for MD5)
macemu_next_deps += [openssl_dep]

macemu_next_inc += [
  include_directories('src/config'),
  include_directories('src/webserver'),
  include_directories('src/webrtc'),
  include_directories('src/drivers/video'),
  include_directories('src/drivers/audio'),
]

mac_phoenix = executable('mac-phoenix',
  macemu_next_sources,
  link_with: macemu_next_link_with,
  dependencies: macemu_next_deps,
  include_directories: macemu_next_inc,
  cpp_args: macemu_next_cpp_args
)

# Tests
if get_option('tests')
  subdir('tests')
endif

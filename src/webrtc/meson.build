# WebRTC signaling server module

webrtc_sources = files([
  'webrtc_server.cpp',
])

webrtc_lib = static_library('webrtc',
  webrtc_sources,
  include_directories: [
    include_directories('../common/include'),
    include_directories('../config'),
    include_directories('../drivers/video/encoders'),
  ],
  dependencies: [libdatachannel_dep, nlohmann_json_dep],
  cpp_args: ['-DWEBRTC_ENABLED'],
)

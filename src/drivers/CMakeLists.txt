# Platform drivers

# --- Drivers ---
add_library(drivers STATIC
  # Video
  video/video_null.cpp
  video/video_screenshot.cpp
  video/video_webrtc.cpp
  video/video_adapter.cpp
  video/video_output.cpp
  video/video_encoder_thread.cpp
  # Audio
  audio/audio_null.cpp
  audio/audio_webrtc.cpp
  audio/audio_direct.cpp
  audio/audio_ipc_reader.cpp
  audio/audio_adapter.cpp
  audio/audio_output.cpp
  audio/audio_encoder_thread.cpp
  # Disk / SCSI / Serial
  disk/disk_null.cpp
  scsi/scsi_null.cpp
  scsi/scsi_adapter.cpp
  serial/serial_adapter.cpp
  serial/serial_unix.cpp
  # Ethernet
  ether/ether_null.cpp
  ether/ether_adapter.cpp
  ether/ether_socket.cpp
  # Platform
  platform/platform_adapter.cpp
  platform/platform_null.cpp
  platform/platform_unix.cpp
  platform/timer_interrupt.cpp
  # MacBrowser (host-side pipeline)
  browser/ring.cpp
  browser/shm.cpp
  browser/module.cpp
  browser/cmd.cpp
  browser/mouse_poll.cpp
  browser/qtwebengine_browser.cpp
  # Consolidated null drivers
  null_drivers.cpp
)

target_include_directories(drivers PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/video
  ${CMAKE_CURRENT_SOURCE_DIR}/audio
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../core
  ${CMAKE_CURRENT_SOURCE_DIR}/../cpu
  ${CMAKE_CURRENT_SOURCE_DIR}/../cpu/uae_cpu
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/../config
  ${CMAKE_CURRENT_SOURCE_DIR}/../webserver
  ${CMAKE_CURRENT_SOURCE_DIR}/../ipc
  ${CMAKE_CURRENT_SOURCE_DIR}/ether
)

find_package(Qt6 6.4 COMPONENTS Core REQUIRED)

target_link_libraries(drivers PRIVATE
  datachannel-static
  codec_flags
  libyuv::libyuv
  Qt6::Core
)

# MacBrowser pixel pipeline (Phase 8): in-process QtWebEngine.
if(BUILD_BROWSER)
  target_link_libraries(drivers PUBLIC
    Qt6::Widgets
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
  )
endif()

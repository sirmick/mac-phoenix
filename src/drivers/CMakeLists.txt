# Platform drivers

# --- lwIP (unity build) ---
set(LWIP_SRC_ROOT ${CMAKE_SOURCE_DIR}/subprojects/lwip/src)

add_library(lwip STATIC
  ether/lwip_build.c
  ether/sys_arch.c
)
target_include_directories(lwip PUBLIC
  ${LWIP_SRC_ROOT}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ether
)
target_include_directories(lwip PRIVATE
  ${LWIP_SRC_ROOT}
)
target_compile_options(lwip PRIVATE -w)

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
  audio/audio_adapter.cpp
  audio/audio_output.cpp
  audio/audio_encoder_thread.cpp
  # Disk / SCSI / Serial
  disk/disk_null.cpp
  scsi/scsi_null.cpp
  scsi/scsi_adapter.cpp
  serial/serial_null.cpp
  serial/serial_adapter.cpp
  # Ethernet
  ether/ether_null.cpp
  ether/ether_adapter.cpp
  ether/ether_lwip.cpp
  ether/ether_raw.cpp
  ether/lwip_nat.cpp
  # Platform
  platform/platform_adapter.cpp
  platform/platform_null.cpp
  platform/platform_unix.cpp
  platform/timer_interrupt.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/ether
)

target_link_libraries(drivers PRIVATE
  datachannel-static
  nlohmann_json::nlohmann_json
  lwip
)

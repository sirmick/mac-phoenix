# WebRTC signaling server module

add_library(webrtc_lib STATIC
  webrtc_server.cpp
)

target_include_directories(webrtc_lib PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../config
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/video/encoders
)

target_compile_definitions(webrtc_lib PRIVATE WEBRTC_ENABLED)

target_link_libraries(webrtc_lib PRIVATE
  datachannel-static
  nlohmann_json::nlohmann_json
)

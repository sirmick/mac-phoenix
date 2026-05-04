# Web Server - HTTP API and browser UI

add_library(webserver STATIC
  http_server.cpp
  http_stream.cpp
  websocket.cpp
  api_handlers.cpp
  static_files.cpp
  file_scanner.cpp
  webserver_main.cpp
)

target_include_directories(webserver PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../config
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/video
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/video/encoders
)

find_package(Qt6 6.4 COMPONENTS Core Network REQUIRED)

target_link_libraries(webserver PRIVATE
  nlohmann_json::nlohmann_json
  codec_flags
  Qt6::Core
  Qt6::Network
)

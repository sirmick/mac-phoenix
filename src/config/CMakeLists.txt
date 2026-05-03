# JSON Configuration System

add_library(config STATIC
  json_utils.cpp
  emulator_config.cpp
  machine_profile.cpp
)

target_include_directories(config PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/video/encoders
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/audio/encoders
)

find_package(Qt6 6.4 COMPONENTS Core REQUIRED)

target_link_libraries(config PRIVATE
  nlohmann_json::nlohmann_json
  Qt6::Core
)

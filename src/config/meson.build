# JSON Configuration System

config_sources = [
  'json_utils.cpp',
  'emulator_config.cpp',
]

config_inc = include_directories(
  '.',
  '../common/include',
  '../drivers/video/encoders',           # For codec.h
  '../drivers/audio/encoders',  # For audio_config.h
)

config_lib = static_library('config',
  config_sources,
  include_directories: config_inc,
  dependencies: [nlohmann_json_dep],
)

config_dep = declare_dependency(
  link_with: config_lib,
  include_directories: config_inc,
)

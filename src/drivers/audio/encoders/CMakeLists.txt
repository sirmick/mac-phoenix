# Audio Encoders

add_library(audio_encoders STATIC
  opus_encoder.cpp
)

target_include_directories(audio_encoders PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
)

# Codec feature flags
target_link_libraries(audio_encoders PUBLIC codec_flags)

# Optional Opus library
if(OPUS_FOUND)
  target_link_libraries(audio_encoders PRIVATE PkgConfig::OPUS)
endif()

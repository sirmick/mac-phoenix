# Video Encoders

add_library(video_encoders STATIC
  h264_encoder.cpp
  vp9_encoder.cpp
  webp_encoder.cpp
  png_encoder.cpp
  fpng.cpp
)

target_include_directories(video_encoders PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/include
)

# Codec feature flags
target_link_libraries(video_encoders PUBLIC codec_flags)

# Optional codec libraries
if(OPENH264_FOUND)
  target_link_libraries(video_encoders PRIVATE PkgConfig::OPENH264)
endif()
if(VPX_FOUND)
  target_link_libraries(video_encoders PRIVATE PkgConfig::VPX)
endif()
if(WEBP_FOUND)
  target_link_libraries(video_encoders PRIVATE PkgConfig::WEBP)
endif()
target_link_libraries(video_encoders PRIVATE libyuv::libyuv)

# fpng needs SSE4.1 + PCLMUL on x86
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
  set_source_files_properties(fpng.cpp PROPERTIES
    COMPILE_OPTIONS "-msse4.1;-mpclmul"
  )
endif()

target_compile_options(video_encoders PRIVATE -Wno-type-limits)

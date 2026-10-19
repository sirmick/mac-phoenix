# Video Encoders

add_library(video_encoders STATIC
  h264_encoder.cpp
  vp9_encoder.cpp
  webp_encoder.cpp
  png_encoder.cpp
  png_write.cpp
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

find_package(ZLIB REQUIRED)
target_link_libraries(video_encoders PRIVATE ZLIB::ZLIB)

target_compile_options(video_encoders PRIVATE -Wno-type-limits)

cmake_minimum_required(VERSION 3.16)
project(mac-phoenix VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Options
option(BUILD_TESTS "Build test suite" ON)
set(TEST_ROM "" CACHE STRING "Path to ROM file for boot tests")
set(TEST_PPC_ROM "" CACHE STRING "Path to PPC ROM for boot tests")
set(TEST_PPC_CDROM "" CACHE STRING "Path to Mac OS 9 ISO for PPC boot tests")

# Codec overrides (AUTO = auto-detect, ON = require, OFF = disable)
set(ENABLE_H264 "AUTO" CACHE STRING "H.264 via OpenH264 (AUTO/ON/OFF)")
set(ENABLE_VP9 "AUTO" CACHE STRING "VP9 via libvpx (AUTO/ON/OFF)")
set(ENABLE_WEBP "AUTO" CACHE STRING "WebP via libwebp (AUTO/ON/OFF)")
set(ENABLE_OPUS "AUTO" CACHE STRING "Opus audio (AUTO/ON/OFF)")

# =============================================================================
# Dependencies
# =============================================================================

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# --- Optional codecs (auto-detected, with manual override) ---
# ENABLE_xxx = AUTO (default), ON (require, fail if missing), OFF (disable)
find_package(PkgConfig QUIET)

# Helper: detect a codec via pkg-config + fallback find_library
macro(detect_codec VAR_NAME PKG_NAME LIB_NAME HEADER_NAME ENABLE_VAR)
  if(${ENABLE_VAR} STREQUAL "OFF")
    set(${VAR_NAME}_FOUND FALSE)
  else()
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(${VAR_NAME} IMPORTED_TARGET ${PKG_NAME})
    endif()
    if(NOT ${VAR_NAME}_FOUND)
      find_library(${VAR_NAME}_LIB NAMES ${LIB_NAME})
      find_path(${VAR_NAME}_INC NAMES ${HEADER_NAME})
      if(${VAR_NAME}_LIB AND ${VAR_NAME}_INC)
        set(${VAR_NAME}_FOUND TRUE)
        add_library(PkgConfig::${VAR_NAME} INTERFACE IMPORTED)
        target_link_libraries(PkgConfig::${VAR_NAME} INTERFACE ${${VAR_NAME}_LIB})
        target_include_directories(PkgConfig::${VAR_NAME} INTERFACE ${${VAR_NAME}_INC})
      endif()
    endif()
    if(${ENABLE_VAR} STREQUAL "ON" AND NOT ${VAR_NAME}_FOUND)
      message(FATAL_ERROR "${VAR_NAME} required (ENABLE set to ON) but not found")
    endif()
  endif()
endmacro()

detect_codec(OPENH264 openh264 openh264 wels/codec_api.h ENABLE_H264)
detect_codec(VPX vpx vpx vpx/vpx_encoder.h ENABLE_VP9)
detect_codec(WEBP libwebp webp webp/encode.h ENABLE_WEBP)
detect_codec(OPUS opus opus opus/opus.h ENABLE_OPUS)

# libyuv (required — encoders use it unconditionally for SIMD color conversion)
find_library(LIBYUV_LIB NAMES yuv REQUIRED)
find_path(LIBYUV_INC NAMES libyuv.h REQUIRED)
add_library(libyuv::libyuv INTERFACE IMPORTED)
target_link_libraries(libyuv::libyuv INTERFACE ${LIBYUV_LIB})
target_include_directories(libyuv::libyuv INTERFACE ${LIBYUV_INC})

# Math library
find_library(M_LIB m)
if(NOT M_LIB)
  set(M_LIB "")
endif()

# --- Subprojects ---

# Unicorn (m68k + ppc CPU engines)
set(UNICORN_ARCH "m68k;ppc" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(UNICORN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UNICORN_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/unicorn EXCLUDE_FROM_ALL)

# libdatachannel (WebRTC)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(USE_NICE OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/libdatachannel EXCLUDE_FROM_ALL)

# nlohmann_json (header-only)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(subprojects/nlohmann_json EXCLUDE_FROM_ALL)

# =============================================================================
# Codec compile definitions (propagated to all targets that need them)
# =============================================================================
add_library(codec_flags INTERFACE)
if(OPENH264_FOUND)
  target_compile_definitions(codec_flags INTERFACE HAVE_OPENH264=1)
endif()
if(VPX_FOUND)
  target_compile_definitions(codec_flags INTERFACE HAVE_VPX=1)
endif()
if(WEBP_FOUND)
  target_compile_definitions(codec_flags INTERFACE HAVE_LIBWEBP=1)
endif()
if(OPUS_FOUND)
  target_compile_definitions(codec_flags INTERFACE HAVE_OPUS=1)
endif()
target_compile_definitions(codec_flags INTERFACE HAVE_LIBYUV=1)

# =============================================================================
# Subdirectories
# =============================================================================
add_subdirectory(src/cpu)
add_subdirectory(src/core)
add_subdirectory(src/drivers)
add_subdirectory(src/ipc)
add_subdirectory(src/config)
add_subdirectory(src/webserver)
add_subdirectory(src/webrtc)
add_subdirectory(src/drivers/video/encoders)
add_subdirectory(src/drivers/audio/encoders)

# =============================================================================
# Main executable
# =============================================================================
add_executable(mac-phoenix src/main.cpp)

# Static libs have circular deps (core <-> cpu <-> kpx, webrtc -> webserver).
# Use --start-group/--end-group to let the linker resolve them.
target_link_libraries(mac-phoenix PRIVATE
  -Wl,--whole-archive cpuemu_ff cpustbl_ff -Wl,--no-whole-archive
  -Wl,--start-group
  core drivers cpu ipc kpx_interp kpx_dyngen uae_cpu uae_jit vm_alloc
  webserver webrtc_lib video_encoders audio_encoders config
  -Wl,--end-group
  unicorn
  Threads::Threads
  ${M_LIB}
  nlohmann_json::nlohmann_json
  datachannel-static
  OpenSSL::SSL
  OpenSSL::Crypto
)

# Optional codec libraries linked at top level
if(OPENH264_FOUND)
  target_link_libraries(mac-phoenix PRIVATE PkgConfig::OPENH264)
endif()
if(VPX_FOUND)
  target_link_libraries(mac-phoenix PRIVATE PkgConfig::VPX)
endif()
if(WEBP_FOUND)
  target_link_libraries(mac-phoenix PRIVATE PkgConfig::WEBP)
endif()
if(OPUS_FOUND)
  target_link_libraries(mac-phoenix PRIVATE PkgConfig::OPUS)
endif()
target_link_libraries(mac-phoenix PRIVATE libyuv::libyuv)

target_include_directories(mac-phoenix PRIVATE
  src/common/include
  src/core
  src/cpu
  src/cpu/uae_cpu
  src/drivers
  src/drivers/platform
  src/config
  src/webserver
  src/webrtc
  src/drivers/video
  src/drivers/audio
  src/ipc
)

target_compile_definitions(mac-phoenix PRIVATE
  HAVE_SIGSEGV_SKIP_INSTRUCTION
  HAVE_SIGINFO_T
)

target_link_libraries(mac-phoenix PRIVATE codec_flags)

# Linker script for PPC mode on Linux x86_64
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  target_link_options(mac-phoenix PRIVATE
    "LINKER:--warn-common"
    "-T${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/linux-x86_64.ld"
  )
else()
  target_link_options(mac-phoenix PRIVATE "LINKER:--warn-common")
endif()

# =============================================================================
# net-bridge (Rust) — Unix-socket NAT bridge spawned when --network socket is set.
# Optional: disable with -DBUILD_NET_BRIDGE=OFF if you don't need networking.
# =============================================================================
option(BUILD_NET_BRIDGE "Build the net-bridge Rust NAT bridge (needs cargo)" ON)

if(BUILD_NET_BRIDGE)
  find_program(CARGO_EXECUTABLE cargo)
  if(NOT CARGO_EXECUTABLE)
    message(STATUS "net-bridge: cargo not found — skipping. Run with -DBUILD_NET_BRIDGE=OFF to silence.")
  else()
    set(NET_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/net-bridge")
    set(NET_BRIDGE_BIN "${NET_BRIDGE_DIR}/target/release/net-bridge")
    set(NET_BRIDGE_LEGACY_SSL "${NET_BRIDGE_DIR}/vendor/openssl-legacy")

    # Rust sources that should trigger a rebuild when they change.
    # (GLOB at configure time — re-run cmake after adding new .rs files.)
    file(GLOB_RECURSE NET_BRIDGE_SOURCES CONFIGURE_DEPENDS
      "${NET_BRIDGE_DIR}/src/*.rs"
    )
    list(APPEND NET_BRIDGE_SOURCES
      "${NET_BRIDGE_DIR}/Cargo.toml"
      "${NET_BRIDGE_DIR}/Cargo.lock"
    )

    # If the vendored legacy OpenSSL has been built (via
    # net-bridge/tools/build-legacy-openssl.sh), link against it so
    # --mitm-tls has SSLv3 + weak ciphers. Otherwise fall back to the
    # system libssl — the crate still builds, it just won't be able to
    # downgrade TLS at runtime.
    if(EXISTS "${NET_BRIDGE_LEGACY_SSL}/lib/libssl.a")
      set(NET_BRIDGE_ENV
        "OPENSSL_DIR=${NET_BRIDGE_LEGACY_SSL}"
        "OPENSSL_STATIC=1"
      )
      message(STATUS "net-bridge: linking vendored OpenSSL at ${NET_BRIDGE_LEGACY_SSL}")
    else()
      set(NET_BRIDGE_ENV "")
      message(STATUS "net-bridge: vendored OpenSSL not built — --mitm-tls will fail at runtime.")
      message(STATUS "            Run net-bridge/tools/build-legacy-openssl.sh then re-run cmake.")
    endif()

    add_custom_command(
      OUTPUT "${NET_BRIDGE_BIN}"
      COMMAND ${CMAKE_COMMAND} -E env ${NET_BRIDGE_ENV}
              "${CARGO_EXECUTABLE}" build --release
              --manifest-path "${NET_BRIDGE_DIR}/Cargo.toml"
      DEPENDS ${NET_BRIDGE_SOURCES}
      WORKING_DIRECTORY "${NET_BRIDGE_DIR}"
      COMMENT "Building net-bridge (Rust release)"
      VERBATIM
    )

    add_custom_target(net-bridge ALL DEPENDS "${NET_BRIDGE_BIN}")
  endif()
endif()

# =============================================================================
# Tests
# =============================================================================
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# =============================================================================
# Configuration summary
# =============================================================================
message(STATUS "")
macro(codec_status NAME FOUND_VAR ENABLE_VAR)
  if(${FOUND_VAR})
    message(STATUS "    ${NAME}  YES")
  elseif(${ENABLE_VAR} STREQUAL "OFF")
    message(STATUS "    ${NAME}  disabled")
  else()
    message(STATUS "    ${NAME}  not found")
  endif()
endmacro()

message(STATUS "")
message(STATUS "=== mac-phoenix build configuration ===")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Codecs:")
codec_status("H.264 (OpenH264) " OPENH264_FOUND ENABLE_H264)
codec_status("VP9   (libvpx)   " VPX_FOUND ENABLE_VP9)
codec_status("WebP  (libwebp)  " WEBP_FOUND ENABLE_WEBP)
codec_status("Opus  (libopus)  " OPUS_FOUND ENABLE_OPUS)
message(STATUS "    libyuv           required")
message(STATUS "    PNG   (fpng)    always")
message(STATUS "")

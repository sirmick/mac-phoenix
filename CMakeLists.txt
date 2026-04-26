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

    # After cargo finishes, drop a symlink into the CMake build tree next
    # to mac-phoenix so find_bridge_binary() in the emulator can resolve
    # it as <exe_dir>/net-bridge — one canonical location, no CWD games.
    set(NET_BRIDGE_LINK "${CMAKE_BINARY_DIR}/net-bridge")

    add_custom_command(
      OUTPUT "${NET_BRIDGE_LINK}"
      COMMAND ${CMAKE_COMMAND} -E env ${NET_BRIDGE_ENV}
              "${CARGO_EXECUTABLE}" build --release
              --manifest-path "${NET_BRIDGE_DIR}/Cargo.toml"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "${NET_BRIDGE_BIN}" "${NET_BRIDGE_LINK}"
      DEPENDS ${NET_BRIDGE_SOURCES}
      WORKING_DIRECTORY "${NET_BRIDGE_DIR}"
      COMMENT "Building net-bridge (Rust release) → ${NET_BRIDGE_LINK}"
      VERBATIM
    )

    add_custom_target(net-bridge ALL DEPENDS "${NET_BRIDGE_LINK}")
  endif()
endif()

# =============================================================================
# BridgeAgent (Retro68 68k Mac app) — guest-side automation helper.
# Installed into :System Folder:Startup Items: on the guest disk during
# boot by command_bridge.cpp's provisioning step. The committed
# BridgeAgent.bin in tests/guest/bridge/ is our fallback; this target
# rebuilds it from source when the Retro68 toolchain is available.
# Opt out with -DBUILD_BRIDGE_AGENT=OFF.
# =============================================================================
option(BUILD_BRIDGE_AGENT "Build the BridgeAgent from source (needs Retro68)" ON)

if(BUILD_BRIDGE_AGENT)
  # Prefer the in-tree toolchain (toolchain/retro68/bin), fall back to
  # whatever's on PATH.
  set(RETRO68_INTREE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain/retro68/bin")
  find_program(RETRO68_GCC m68k-apple-macos-gcc
    HINTS "${RETRO68_INTREE}" NO_DEFAULT_PATH)
  if(NOT RETRO68_GCC)
    find_program(RETRO68_GCC m68k-apple-macos-gcc)
  endif()
  find_program(RETRO68_REZ Rez
    HINTS "${RETRO68_INTREE}" NO_DEFAULT_PATH)
  if(NOT RETRO68_REZ)
    find_program(RETRO68_REZ Rez)
  endif()

  if(NOT RETRO68_GCC OR NOT RETRO68_REZ)
    message(STATUS "bridge-agent: Retro68 toolchain not found — keeping committed BridgeAgent.bin.")
    message(STATUS "              Set -DBUILD_BRIDGE_AGENT=OFF to silence this message.")
  else()
    get_filename_component(RETRO68_BIN_DIR "${RETRO68_GCC}" DIRECTORY)
    get_filename_component(RETRO68_ROOT "${RETRO68_BIN_DIR}" DIRECTORY)
    set(RETRO68_RINCLUDES "${RETRO68_ROOT}/m68k-apple-macos/RIncludes")

    # Apple Universal Interfaces 3.4 — the toolchain's default Multiversal
    # headers are missing several files bridge_agent uses (Script.h,
    # Aliases.h, ShutDown.h, Scrap.h, Processes.r). If the user has UI 3.4
    # at private/Universal Interfaces/Universal/Interfaces/, prepend its
    # CIncludes/RIncludes to the gcc/Rez search paths so the build works
    # against vanilla Retro68 (no need to install UI 3.4 into the toolchain
    # tree itself). Apple's headers can't be redistributed in source, hence
    # the gitignored private/ location.
    set(BA_UI3 "${CMAKE_CURRENT_SOURCE_DIR}/private/Universal Interfaces/Universal/Interfaces")
    # Retro68's gcc preasserts cpu=m68k but not system=macos; UI 3.4's
    # ConditionalMacros.h needs both for its #if #system(macos) check or it
    # falls through to "#error unsupported GNU C compiler".
    set(BA_GCC_EXTRA_FLAGS -A system=macos)
    set(BA_REZ_EXTRA_FLAGS "")
    if(EXISTS "${BA_UI3}/CIncludes/Script.h")
      # -idirafter (not -I): Retro68's Multiversal headers win for Files.h /
      # Memory.h / etc. (which provide gcc-style inline trap glue), and UI 3.4
      # only fills in headers Multiversal lacks (Script.h, Aliases.h,
      # ShutDown.h, Scrap.h). With -I, UI 3.4's Files.h would shadow
      # Multiversal and break linking — its FSWrite et al. are plain externs
      # against an MPW/CodeWarrior runtime we don't have.
      list(APPEND BA_GCC_EXTRA_FLAGS -idirafter "${BA_UI3}/CIncludes")
      set(BA_REZ_EXTRA_FLAGS -I "${BA_UI3}/RIncludes")
      message(STATUS "bridge-agent: using Apple UI 3.4 from private/")
    else()
      message(STATUS "bridge-agent: Apple UI 3.4 not present at private/Universal Interfaces/")
      message(STATUS "              — bridge_agent.c needs Script.h/Aliases.h/ShutDown.h/Scrap.h.")
      message(STATUS "              Either drop UI 3.4 there or set BUILD_BRIDGE_AGENT=OFF.")
    endif()

    set(BA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/guest/bridge")
    set(BA_CODE "${BA_DIR}/BridgeAgent.code.bin")
    set(BA_BIN  "${BA_DIR}/BridgeAgent.bin")

    # Compile C → Retro68 code binary
    add_custom_command(
      OUTPUT "${BA_CODE}"
      COMMAND "${RETRO68_GCC}" ${BA_GCC_EXTRA_FLAGS} -O2 -o "${BA_CODE}" bridge_agent.c
      DEPENDS "${BA_DIR}/bridge_agent.c"
      WORKING_DIRECTORY "${BA_DIR}"
      COMMENT "Building BridgeAgent (m68k): bridge_agent.c → BridgeAgent.code.bin"
      VERBATIM
    )

    # Rez the resource fork + pack into MacBinary (.bin), APPL (APPL fork),
    # and disk image (.dsk). The committed .bin is what command_bridge
    # installs into the guest's Startup Items.
    add_custom_command(
      OUTPUT "${BA_BIN}"
      COMMAND "${RETRO68_REZ}" "${RETRO68_RINCLUDES}/Retro68APPL.r" bridge_agent.r
              -I "${RETRO68_RINCLUDES}"
              ${BA_REZ_EXTRA_FLAGS}
              --copy "${BA_CODE}"
              -o "${BA_BIN}"
              -t APPL -c MxBr
              --cc "${BA_DIR}/BridgeAgent.dsk"
              --cc "${BA_DIR}/BridgeAgent.APPL"
      DEPENDS "${BA_CODE}" "${BA_DIR}/bridge_agent.r"
      WORKING_DIRECTORY "${BA_DIR}"
      COMMENT "Rezzing BridgeAgent.bin"
      VERBATIM
    )

    add_custom_target(bridge-agent ALL DEPENDS "${BA_BIN}")
    message(STATUS "bridge-agent: using Retro68 at ${RETRO68_BIN_DIR}")
  endif()
endif()

# =============================================================================
# Install rules (used by .deb / .rpm packaging — see packaging/ and debian/)
#
# Layout when installed with -DCMAKE_INSTALL_PREFIX=/usr:
#   /usr/bin/mac-phoenix                 — emulator
#   /usr/bin/net-bridge                  — Rust NAT bridge (if built)
#   /usr/share/mac-phoenix/client/       — web UI assets
#   /usr/share/mac-phoenix/BridgeAgent.bin
#   /usr/share/mac-phoenix/provisioning/install_bridge_agent.sh
#   /usr/share/applications/mac-phoenix.desktop
#   /usr/share/doc/mac-phoenix/{README.md,CLAUDE.md,LICENSE}
#
# Runtime path resolution lives in:
#   src/config/emulator_config.cpp        — client_dir fallback
#   src/drivers/ether/ether_socket.cpp    — find_bridge_binary()
#   src/core/command_bridge.cpp           — resolve_repo_relative()
# =============================================================================
include(GNUInstallDirs)

install(TARGETS mac-phoenix
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_NET_BRIDGE AND CARGO_EXECUTABLE)
  install(PROGRAMS "${NET_BRIDGE_BIN}"
          DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY client/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mac-phoenix/client
        FILES_MATCHING
        PATTERN "*.html"
        PATTERN "*.js"
        PATTERN "*.css"
        PATTERN "*.svg"
        PATTERN "*.json")

install(FILES tests/guest/bridge/BridgeAgent.bin
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mac-phoenix)

install(PROGRAMS provisioning/install_bridge_agent.sh
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mac-phoenix/provisioning)

install(FILES packaging/mac-phoenix.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        OPTIONAL)

install(FILES README.md CLAUDE.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        OPTIONAL)

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

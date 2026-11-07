# Executor as a desktop application (README.md): Qt 6 Widgets, Executor
# in-process through executor_host and executor/host (session.h), sound
# through miniaudio (one header, public domain / MIT-0). Native builds only.
find_package(Qt6 6.4 COMPONENTS Core Gui Widgets)
if(NOT Qt6Widgets_FOUND)
    message(STATUS "Executor desktop app: Qt 6 Widgets not found, not built")
    return()
endif()

# The settings dialog is made from the tab's schema (executor/web), the one
# the server validates with (executor/server keeps a copy, the same bytes):
# compiled in from there, so the three never differ.
set(DESKTOP_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/../../web/settings.schema.json)
configure_file(${DESKTOP_SCHEMA} ${CMAKE_CURRENT_BINARY_DIR}/settings.schema.json COPYONLY)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/desktop.qrc
    "<RCC><qresource prefix=\"/\"><file>settings.schema.json</file></qresource></RCC>\n")

add_executable(executor-desktop
    main.cpp
    emulator.h
    emulator.cpp
    screen_widget.h
    screen_widget.cpp
    keys.h
    keys.cpp
    audio.h
    audio.cpp
    settings.h
    settings.cpp
    settings_dialog.h
    settings_dialog.cpp
    disks.h
    disks.cpp
    log_capture.h
    log_capture.cpp
    main_window.h
    main_window.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/desktop.qrc
)
set_target_properties(executor-desktop PROPERTIES
    AUTOMOC ON AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(executor-desktop PRIVATE third_party/miniaudio)

# The version (git describe) in the window's title and the log's first line
if(NOT EXECUTOR_VERSION)
    execute_process(COMMAND git describe --always --dirty --tags
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE EXECUTOR_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT EXECUTOR_VERSION)
        set(EXECUTOR_VERSION unknown)
    endif()
endif()
target_compile_definitions(executor-desktop PRIVATE EXECUTOR_VERSION="${EXECUTOR_VERSION}")

# Windows (cmake/ExecutorWindows.cmake): a GUI executable (--console for a
# console), its manifest (UTF-8 code page), icon and version, and the crash
# report (windows_support.h: minidumps through dbghelp)
if(WIN32)
    configure_file(windows/executor-desktop.rc.in ${CMAKE_CURRENT_BINARY_DIR}/executor-desktop.rc @ONLY)
    target_sources(executor-desktop PRIVATE windows_support.h windows_support.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/executor-desktop.rc)
    set_target_properties(executor-desktop PROPERTIES WIN32_EXECUTABLE ON)
    target_link_libraries(executor-desktop PRIVATE dbghelp)
endif()
target_link_libraries(executor-desktop PRIVATE
    executor-host executor_platform_glue Qt6::Core Qt6::Gui Qt6::Widgets ${CMAKE_DL_LIBS})
target_compile_options(executor-desktop PRIVATE ${EXECUTOR_COMMON_FLAGS})
target_compile_features(executor-desktop PRIVATE cxx_std_17)
phoenix_exempt_from_hardening(executor-desktop)

# A Linux AppImage (tools/desktop/appimage.sh: linuxdeploy and its Qt
# plugin, fetched once): BUILD/Executor-x86_64.AppImage
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_custom_target(executor-desktop-appimage
        COMMAND ${PROJECT_SOURCE_DIR}/tools/desktop/appimage.sh ${CMAKE_BINARY_DIR} ${CMAKE_BINARY_DIR}
        DEPENDS executor-desktop
        USES_TERMINAL)
endif()

# Offscreen to the Finder (executor/tests/desktop_finder.sh): Executor's
# own System, and Apple's 7.6.1 from the web library with a disk mounted and
# written through its overlay (skipped when the library isn't there).
if(BUILD_TESTS)
    set(_desktop_test ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/desktop_finder.sh)
    set(EXECUTOR_WEB_LIBRARY "$ENV{HOME}/storage/web-library" CACHE PATH
        "The web library's disk images (tools/web/make_library.py)")
    set(_library ${EXECUTOR_WEB_LIBRARY})
    add_test(NAME executor.desktop.finder
        COMMAND ${_desktop_test} $<TARGET_FILE:executor-desktop> ${CMAKE_CURRENT_BINARY_DIR}/test-finder)
    add_test(NAME executor.desktop.apple761
        COMMAND ${_desktop_test} $<TARGET_FILE:executor-desktop> ${CMAKE_CURRENT_BINARY_DIR}/test-apple761
                ${_library}/boot-7.6.1.img ${_library}/utilities.img Utilities)
    set_tests_properties(executor.desktop.finder executor.desktop.apple761 PROPERTIES
        LABELS executor TIMEOUT 150 SKIP_RETURN_CODE 77)

    # settings.json is executor/server's (executor/tests/desktop_settings.cpp)
    add_executable(executor_desktop_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/desktop_settings.cpp
        settings.h settings.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/desktop.qrc)
    set_target_properties(executor_desktop_tests PROPERTIES AUTOMOC ON AUTORCC ON)
    target_include_directories(executor_desktop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(executor_desktop_tests PRIVATE
        EXECUTOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")
    target_link_libraries(executor_desktop_tests PRIVATE gtest Qt6::Core)
    target_compile_features(executor_desktop_tests PRIVATE cxx_std_17)
    phoenix_exempt_from_hardening(executor_desktop_tests)
    add_test(NAME executor.desktop.settings COMMAND executor_desktop_tests)
    set_tests_properties(executor.desktop.settings PROPERTIES LABELS executor TIMEOUT 30)
endif()

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
target_link_libraries(executor-desktop PRIVATE
    executor-host executor_platform_glue Qt6::Core Qt6::Gui Qt6::Widgets ${CMAKE_DL_LIBS})
target_compile_options(executor-desktop PRIVATE ${EXECUTOR_COMMON_FLAGS})
target_compile_features(executor-desktop PRIVATE cxx_std_17)
phoenix_exempt_from_hardening(executor-desktop)

# Offscreen to the Finder (executor/tests/desktop_finder.sh): Executor's
# own System, and Apple's 7.6.1 from the web library with a disk mounted
# (skipped when the library isn't there).
if(BUILD_TESTS)
    set(_desktop_test ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/desktop_finder.sh)
    set(EXECUTOR_WEB_LIBRARY "$ENV{HOME}/storage/web-library" CACHE PATH
        "The web library's disk images (tools/web/make_library.py)")
    set(_library ${EXECUTOR_WEB_LIBRARY})
    add_test(NAME executor.desktop.finder
        COMMAND ${_desktop_test} $<TARGET_FILE:executor-desktop> ${CMAKE_CURRENT_BINARY_DIR}/test-finder)
    add_test(NAME executor.desktop.apple761
        COMMAND ${_desktop_test} $<TARGET_FILE:executor-desktop> ${CMAKE_CURRENT_BINARY_DIR}/test-apple761
                ${_library}/boot-7.6.1.img ${_library}/utilities.img)
    set_tests_properties(executor.desktop.finder executor.desktop.apple761 PROPERTIES
        LABELS executor TIMEOUT 150 SKIP_RETURN_CODE 77)
endif()

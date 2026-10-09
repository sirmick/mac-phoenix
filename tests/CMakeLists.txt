# Test suite for mac-phoenix
#
# Run with: ctest --test-dir build
# Verbose:  ctest --test-dir build -V
# Specific: ctest --test-dir build -R api_endpoints

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# --- Fast tests (no ROM / no boot required) ---

add_executable(test_mac_roman
  ${TEST_DIR}/test_mac_roman.cpp
  ${CMAKE_SOURCE_DIR}/src/core/mac_roman.cpp
)
target_include_directories(test_mac_roman PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
add_test(NAME mac_roman COMMAND test_mac_roman)
set_tests_properties(mac_roman PROPERTIES TIMEOUT 5 LABELS "unit")

# BrowserShm SPSC ring protocol — host-only, no emulator. Compiles
# ring.cpp directly so we exercise the exact code shm.cpp links against.
add_executable(test_browser_shm
  ${TEST_DIR}/test_browser_shm.cpp
  ${CMAKE_SOURCE_DIR}/src/drivers/browser/ring.cpp
)
target_include_directories(test_browser_shm PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/common/include
  ${CMAKE_SOURCE_DIR}/src/drivers
)
add_test(NAME browser_shm COMMAND test_browser_shm)
set_tests_properties(browser_shm PROPERTIES TIMEOUT 10 LABELS "unit")

add_test(
  NAME api_endpoints
  COMMAND bash ${TEST_DIR}/test_api_endpoints.sh
)
set_tests_properties(api_endpoints PROPERTIES
  TIMEOUT 30
  LABELS "api"
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME config_api
  COMMAND bash ${TEST_DIR}/test_config_api.sh 18092
)
set_tests_properties(config_api PROPERTIES
  TIMEOUT 30
  LABELS "api"
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME extfs
  COMMAND bash ${TEST_DIR}/test_extfs.sh 18098
)
set_tests_properties(extfs PROPERTIES
  TIMEOUT 60
  LABELS "api"
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

# --- Boot tests (require ROM, longer timeout) ---

set(TEST_SE_ROM "" CACHE STRING "Path to Mac SE ROM for boot tests")

add_test(NAME boot_se
    COMMAND ${CMAKE_SOURCE_DIR}/tests/test_boot_se.sh --timeout 30
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(boot_se PROPERTIES
    TIMEOUT 45
    LABELS "boot"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_SE_ROM=${TEST_SE_ROM}")

add_test(
  NAME boot_uae_interp
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend uae --no-jit --timeout 20 --port 18080
)
set_tests_properties(boot_uae_interp PROPERTIES
  TIMEOUT 35
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME boot_uae_jit
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend uae --jit --timeout 20 --port 18082
)
set_tests_properties(boot_uae_jit PROPERTIES
  TIMEOUT 35
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME command_bridge
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --port 18106
)
set_tests_properties(command_bridge PROPERTIES
  TIMEOUT 30
  LABELS "boot"
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME wne_patch_sanity
  COMMAND bash ${TEST_DIR}/test_wne_patch_sanity.sh --port 18108
)
set_tests_properties(wne_patch_sanity PROPERTIES
  TIMEOUT 60
  LABELS "bridge"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME boot_ppc_interp
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --no-jit --timeout 45 --port 18086
)
set_tests_properties(boot_ppc_interp PROPERTIES
  TIMEOUT 60
  LABELS "boot"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_jit
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --jit --timeout 45 --port 18088
)
set_tests_properties(boot_ppc_jit PROPERTIES
  TIMEOUT 60
  LABELS "boot"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_api
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --timeout 45 --port 18090 --webserver
)
set_tests_properties(boot_ppc_api PROPERTIES
  TIMEOUT 60
  LABELS "boot"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME mouse_position
  COMMAND bash ${TEST_DIR}/test_mouse_position.sh --backend uae --timeout 15 --port 18096
)
set_tests_properties(mouse_position PROPERTIES
  TIMEOUT 30
  LABELS "boot"
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

# --- Guest test suite (requires Retro68-built MacTestSuite binary) ---

add_test(
  NAME guest_suite
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --timeout 60 --port 18094
)
set_tests_properties(guest_suite PROPERTIES
  TIMEOUT 90
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME guest_suite_761
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --os-version 7.6.1 --timeout 60 --port 18118
)
set_tests_properties(guest_suite_761 PROPERTIES
  TIMEOUT 90
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

# --- Finder test set: AppleScript drives the Finder (make/rename/duplicate/
# move/copy to another volume/windows/trash), MacPerl checks the disk.

add_test(
  NAME finder_suite
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --script FinderScriptTest.pl --results finder_results.txt --network none --timeout 120 --port 18114
)
set_tests_properties(finder_suite PROPERTIES
  TIMEOUT 160
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME finder_suite_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --script FinderScriptTest.pl --results finder_results.txt --network none --timeout 150 --port 18116
)
set_tests_properties(finder_suite_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# Host keystrokes (/api/keypress) reach a MacPerl script's <STDIN>.
add_test(
  NAME keyboard_guest
  COMMAND bash ${TEST_DIR}/test_keyboard_guest.sh --timeout 90 --port 18120
)
set_tests_properties(keyboard_guest PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME keyboard_guest_executor
  COMMAND bash ${TEST_DIR}/test_keyboard_guest.sh --backend executor --timeout 90 --port 18122
)
set_tests_properties(keyboard_guest_executor PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# --- Executor variants: Apple's 7.5.5 System on Executor's Toolbox, no ROM.
# Runs on the System Folder of the test copy of macos-7.5.5.img (extracted
# into <storage>/executor-systems on first use). The guest suite's network
# tests fail until Executor has a TCP/IP stack.

add_test(
  NAME command_bridge_executor
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend executor --timeout 60 --port 18110
)
set_tests_properties(command_bridge_executor PROPERTIES
  TIMEOUT 90
  LABELS "boot"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME guest_suite_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --timeout 150 --port 18112
)
set_tests_properties(guest_suite_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# The ROM-based 68k Mac on the Musashi core (--core musashi).
add_test(NAME boot_musashi
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend uae --core musashi --timeout 30 --port 18140)
set_tests_properties(boot_musashi PROPERTIES TIMEOUT 60 LABELS "boot;musashi" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME boot_se_musashi
  COMMAND ${CMAKE_SOURCE_DIR}/tests/test_boot_se.sh --core musashi --timeout 30 --port 18142
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(boot_se_musashi PROPERTIES TIMEOUT 60 LABELS "boot;musashi" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_SE_ROM=${TEST_SE_ROM}")

add_test(NAME command_bridge_musashi
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --core musashi --port 18144)
set_tests_properties(command_bridge_musashi PROPERTIES TIMEOUT 90 LABELS "boot;musashi" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME guest_suite_musashi
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --core musashi --timeout 60 --port 18146)
set_tests_properties(guest_suite_musashi PROPERTIES TIMEOUT 120 LABELS "guest;musashi" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME finder_suite_musashi
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --core musashi --script FinderScriptTest.pl --results finder_results.txt --network none --timeout 120 --port 18148)
set_tests_properties(finder_suite_musashi PROPERTIES TIMEOUT 180 LABELS "guest;musashi" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME keyboard_guest_musashi
  COMMAND bash ${TEST_DIR}/test_keyboard_guest.sh --core musashi --timeout 90 --port 18150)
set_tests_properties(keyboard_guest_musashi PROPERTIES TIMEOUT 130 LABELS "guest;musashi" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

# The same Executor runs on the Musashi 68k core (--core musashi).
add_test(
  NAME command_bridge_executor_musashi
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend executor --core musashi --timeout 60 --port 18130
)
set_tests_properties(command_bridge_executor_musashi PROPERTIES
  TIMEOUT 90
  LABELS "boot;musashi"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME guest_suite_executor_musashi
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --core musashi --timeout 150 --port 18132
)
set_tests_properties(guest_suite_executor_musashi PROPERTIES
  TIMEOUT 200
  LABELS "guest;musashi"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME finder_suite_executor_musashi
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --core musashi --script FinderScriptTest.pl --results finder_results.txt --network none --timeout 150 --port 18134
)
set_tests_properties(finder_suite_executor_musashi PROPERTIES
  TIMEOUT 200
  LABELS "guest;musashi"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME keyboard_guest_executor_musashi
  COMMAND bash ${TEST_DIR}/test_keyboard_guest.sh --backend executor --core musashi --timeout 90 --port 18136
)
set_tests_properties(keyboard_guest_executor_musashi PROPERTIES
  TIMEOUT 130
  LABELS "guest;musashi"
  SKIP_RETURN_CODE 77
)

# --- PPC variants of API/boot tests ---

add_test(
  NAME command_bridge_ppc
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend kpx --timeout 20 --port 18100
)
set_tests_properties(command_bridge_ppc PROPERTIES
  TIMEOUT 35
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME mouse_position_ppc
  COMMAND bash ${TEST_DIR}/test_mouse_position.sh --backend kpx --timeout 20 --port 18102
)
set_tests_properties(mouse_position_ppc PROPERTIES
  TIMEOUT 35
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME guest_suite_ppc
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --timeout 60 --port 18104
)
set_tests_properties(guest_suite_ppc PROPERTIES
  TIMEOUT 90
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)



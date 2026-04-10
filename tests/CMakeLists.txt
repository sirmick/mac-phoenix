# Test suite for mac-phoenix
#
# Run with: ctest --test-dir build
# Verbose:  ctest --test-dir build -V
# Specific: ctest --test-dir build -R api_endpoints

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# --- Fast tests (no ROM / no boot required) ---

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
  NAME boot_unicorn
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend unicorn --timeout 120 --port 18084
)
set_tests_properties(boot_unicorn PROPERTIES
  TIMEOUT 180
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME command_bridge
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh
)
set_tests_properties(command_bridge PROPERTIES
  TIMEOUT 30
  LABELS "boot"
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME boot_ppc_interp
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --no-ppc-jit --timeout 45 --port 18086
)
set_tests_properties(boot_ppc_interp PROPERTIES
  TIMEOUT 60
  LABELS "boot"
  ENVIRONMENT "MACEMU_PPC_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_jit
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --ppc-jit --timeout 45 --port 18088
)
set_tests_properties(boot_ppc_jit PROPERTIES
  TIMEOUT 60
  LABELS "boot"
  ENVIRONMENT "MACEMU_PPC_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_api
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --timeout 45 --port 18090 --webserver
)
set_tests_properties(boot_ppc_api PROPERTIES
  TIMEOUT 60
  LABELS "boot"
  ENVIRONMENT "MACEMU_PPC_ROM=${TEST_PPC_ROM}"
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

# --- PPC variants of API/boot tests ---

add_test(
  NAME command_bridge_ppc
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend kpx --arch ppc --timeout 20 --port 18100
)
set_tests_properties(command_bridge_ppc PROPERTIES
  TIMEOUT 35
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME mouse_position_ppc
  COMMAND bash ${TEST_DIR}/test_mouse_position.sh --backend kpx --arch ppc --timeout 20 --port 18102
)
set_tests_properties(mouse_position_ppc PROPERTIES
  TIMEOUT 35
  LABELS "boot"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME guest_suite_ppc
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --arch ppc --timeout 60 --port 18104
)
set_tests_properties(guest_suite_ppc PROPERTIES
  TIMEOUT 90
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

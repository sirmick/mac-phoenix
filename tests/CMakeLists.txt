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

# Two mame-68k cores in one process (docs/cpu/PLAN.md, C1c). Host-only.
add_executable(test_cpu_cores
  ${TEST_DIR}/test_cpu_cores.cpp
  ${CMAKE_SOURCE_DIR}/src/cpu/core/guest_memory.cpp
)
target_include_directories(test_cpu_cores PRIVATE ${CMAKE_SOURCE_DIR}/src/cpu/core)
target_link_libraries(test_cpu_cores PRIVATE mame_m68k mame_ppc)
target_compile_features(test_cpu_cores PRIVATE cxx_std_17)
add_test(NAME cpu_cores COMMAND test_cpu_cores)
set_tests_properties(cpu_cores PROPERTIES TIMEOUT 10 LABELS "unit;mame")

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

# The same boot on MAME's Musashi (docs/cpu/PLAN.md, C1).
add_test(
  NAME boot_core_mame68k
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend uae --core mame-68k --timeout 25 --port 18114
)
set_tests_properties(boot_core_mame68k PROPERTIES
  TIMEOUT 40
  LABELS "boot;mame"
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

# The same boot on MAME's PowerPC core (docs/cpu/PLAN.md, C2b): UML
# interpreted until the native back-ends land, so slower.
add_test(
  NAME boot_ppc_mame
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --core mame-ppc --timeout 45 --port 18116
)
set_tests_properties(boot_ppc_mame PROPERTIES
  TIMEOUT 65
  LABELS "boot;mame"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_mame_jit
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --core mame-ppc --jit --timeout 30 --port 18120 --webserver
)
set_tests_properties(boot_ppc_mame_jit PROPERTIES
  TIMEOUT 50
  LABELS "boot;mame"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_mame_api
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --core mame-ppc --timeout 60 --port 18118 --webserver
)
set_tests_properties(boot_ppc_mame_api PROPERTIES
  TIMEOUT 80
  LABELS "boot;mame"
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

# --- Guest test suite (MacPhoenixSuite, tests/guest/suite: built with tools/mpw/mpw) ---

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
# move/copy to another volume/windows/trash), the File Manager checks the
# disk (MacPhoenixSuite's finder section).

add_test(
  NAME finder_suite
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --sections finder --results finder_results.txt --network none --timeout 120 --port 18114
)
set_tests_properties(finder_suite PROPERTIES
  TIMEOUT 160
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME finder_suite_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --sections finder --results finder_results.txt --network none --timeout 150 --port 18116
)
set_tests_properties(finder_suite_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# MacPerl as a third-party application: launched by a do-script Apple Event
# (/api/script), runs Perl, reaches the Host volume and AppleScript.
add_test(
  NAME macperl_smoke
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --script MacPerlSmoke.pl --results macperl_results.txt --network none --timeout 90 --port 18130
)
set_tests_properties(macperl_smoke PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME macperl_smoke_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --script MacPerlSmoke.pl --results macperl_results.txt --network none --timeout 150 --port 18132
)
set_tests_properties(macperl_smoke_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The bridge trap ($ABE5, guest_bridge.h) and an event-driven wake-up
# (Time Manager task -> WakeUpProcess), what the next BridgeAgent builds on:
# tests/guest/bridgedemo. 68k, PowerPC (9.0.4) and Executor.
add_test(
  NAME bridge_trap
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --app ${CMAKE_SOURCE_DIR}/tests/guest/bridgedemo/BridgeDemo.bin --results bridge_demo.txt --network none --timeout 90 --port 18134
)
set_tests_properties(bridge_trap PROPERTIES
  TIMEOUT 130
  LABELS "guest;bridge"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME bridge_trap_ppc
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --app ${CMAKE_SOURCE_DIR}/tests/guest/bridgedemo/BridgeDemo.bin --results bridge_demo.txt --network none --timeout 90 --port 18136
)
set_tests_properties(bridge_trap_ppc PROPERTIES
  TIMEOUT 130
  LABELS "guest;bridge"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME bridge_trap_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/bridgedemo/BridgeDemo.bin --results bridge_demo.txt --network none --timeout 150 --port 18138
)
set_tests_properties(bridge_trap_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;bridge;executor"
  SKIP_RETURN_CODE 77
)

# Host keystrokes (/api/keypress) reach the front application (the suite).
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

add_test(
  NAME drag_executor
  COMMAND bash ${TEST_DIR}/test_drag_executor.sh --timeout 90 --port 18124
)
set_tests_properties(drag_executor PROPERTIES
  TIMEOUT 130
  LABELS "guest;executor"
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

# The ROM-based 68k Mac on MAME's Musashi (--core mame-68k).
add_test(NAME boot_mame68k
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend uae --core mame-68k --timeout 30 --port 18140)
set_tests_properties(boot_mame68k PROPERTIES TIMEOUT 60 LABELS "boot;mame" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME boot_se_mame68k
  COMMAND ${CMAKE_SOURCE_DIR}/tests/test_boot_se.sh --core mame-68k --timeout 30 --port 18142
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(boot_se_mame68k PROPERTIES TIMEOUT 60 LABELS "boot;mame" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_SE_ROM=${TEST_SE_ROM}")

add_test(NAME command_bridge_mame68k
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --core mame-68k --port 18144)
set_tests_properties(command_bridge_mame68k PROPERTIES TIMEOUT 90 LABELS "boot;mame" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME guest_suite_mame68k
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --core mame-68k --timeout 120 --port 18146)
set_tests_properties(guest_suite_mame68k PROPERTIES TIMEOUT 160 LABELS "guest;mame" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME finder_suite_mame68k
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --core mame-68k --sections finder --results finder_results.txt --network none --timeout 120 --port 18148)
set_tests_properties(finder_suite_mame68k PROPERTIES TIMEOUT 180 LABELS "guest;mame" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

add_test(NAME keyboard_guest_mame68k
  COMMAND bash ${TEST_DIR}/test_keyboard_guest.sh --core mame-68k --timeout 90 --port 18150)
set_tests_properties(keyboard_guest_mame68k PROPERTIES TIMEOUT 130 LABELS "guest;mame" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")

# The same Executor on MAME's Musashi (--core mame-68k; docs/cpu/PLAN.md, S12).
add_test(
  NAME command_bridge_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend executor --core mame-68k --timeout 60 --port 18152
)
set_tests_properties(command_bridge_executor_mame68k PROPERTIES
  TIMEOUT 90
  LABELS "boot;mame"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME guest_suite_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --core mame-68k --timeout 150 --port 18154
)
set_tests_properties(guest_suite_executor_mame68k PROPERTIES
  TIMEOUT 200
  LABELS "guest;mame"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME finder_suite_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --core mame-68k --sections finder --results finder_results.txt --network none --timeout 150 --port 18156
)
set_tests_properties(finder_suite_executor_mame68k PROPERTIES
  TIMEOUT 200
  LABELS "guest;mame"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME keyboard_guest_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_keyboard_guest.sh --backend executor --core mame-68k --timeout 90 --port 18158
)
set_tests_properties(keyboard_guest_executor_mame68k PROPERTIES
  TIMEOUT 130
  LABELS "guest;mame"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME drag_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_drag_executor.sh --core mame-68k --timeout 90 --port 18160
)
set_tests_properties(drag_executor_mame68k PROPERTIES
  TIMEOUT 130
  LABELS "guest;mame;executor"
  SKIP_RETURN_CODE 77
)

# Apple's Shared Library Manager (started by the System's 'AINI' -32700)
# through Apple's ASLM test tools in MPW Shell; skips without the ASLM kit
# in ~/storage/aslm-kit.
add_test(
  NAME aslm_executor
  COMMAND bash ${TEST_DIR}/test_aslm_executor.sh --timeout 300 --port 18164
)
set_tests_properties(aslm_executor PROPERTIES
  TIMEOUT 340
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

add_test(
  NAME aslm_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_aslm_executor.sh --core mame-68k --timeout 300 --port 18166
)
set_tests_properties(aslm_executor_mame68k PROPERTIES
  TIMEOUT 340
  LABELS "guest;mame;executor"
  SKIP_RETURN_CODE 77
)

# Executor's Toolbox gtests built with Retro68 into a 68k app and run in
# the guest: on Apple's ROM and System (the ground truth) and on Executor,
# both cores. Skips without Retro68 (toolchain/retro68). The cells share
# the app's build directory (build-m68k-tests), hence the lock.
foreach(cell uae executor executor_mame68k)
  if(cell STREQUAL "uae")
    set(args --backend uae --port 18410)
    set(labels "guest")
  elseif(cell STREQUAL "executor")
    set(args --backend executor --port 18412)
    set(labels "guest;executor")
  else()
    set(args --backend executor --core mame-68k --port 18414)
    set(labels "guest;mame;executor")
  endif()
  add_test(
    NAME toolbox_suite_${cell}
    COMMAND bash ${TEST_DIR}/test_toolbox_suite.sh ${args} --timeout 180
  )
  set_tests_properties(toolbox_suite_${cell} PROPERTIES
    TIMEOUT 300
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK toolbox_app
  )
endforeach()

# Apple's MPW (SC, Link, ToolServer) on Executor through tools/mpw/mpw;
# skips without the MPW disk (tools/mpw/mpw setup).
add_test(
  NAME mpw_toolchain_executor
  COMMAND bash ${TEST_DIR}/test_mpw_toolchain.sh --port 18392
)
set_tests_properties(mpw_toolchain_executor PROPERTIES
  TIMEOUT 300
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The committed guest programs (MacPhoenixSuite, OTTest) are their sources'
# builds: each is built again with tools/mpw/mpw and compared.
add_test(
  NAME guest_builds_current
  COMMAND bash ${TEST_DIR}/test_guest_builds.sh --port 18396
)
set_tests_properties(guest_builds_current PROPERTIES
  TIMEOUT 300
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# Executor's native Open Transport through Apple's 68k OT glue (OTTest, an
# MPW tool run in ToolServer); on both cores. Skips without the MPW disk.
foreach(core uae mame-68k)
  if(core STREQUAL "uae")
    set(name ot_executor)
    set(port 18396)
  else()
    set(name ot_executor_mame68k)
    set(port 18398)
  endif()
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_ot_executor.sh --core ${core} --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 300
    LABELS "guest;executor"
    SKIP_RETURN_CODE 77
  )
endforeach()

# Thread Manager through Frontier's UserTalk threads (process.cpp), on
# both cores.
add_test(
  NAME threads_executor
  COMMAND bash ${TEST_DIR}/test_threads_executor.sh --timeout 240 --port 18168
)
set_tests_properties(threads_executor PROPERTIES
  TIMEOUT 280
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME threads_executor_mame68k
  COMMAND bash ${TEST_DIR}/test_threads_executor.sh --core mame-68k --timeout 240 --port 18162
)
set_tests_properties(threads_executor_mame68k PROPERTIES
  TIMEOUT 280
  LABELS "guest;mame;executor"
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

# C3a: host-initiated 68k routines on a mame-68k core beside mame-ppc
# (--m68k-host 1; docs/cpu/PLAN.md).
add_test(
  NAME command_bridge_ppc_mame_m68k
  COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend kpx --core mame-ppc --jit --m68k-host 1 --timeout 20 --port 18126
)
set_tests_properties(command_bridge_ppc_mame_m68k PROPERTIES
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

# The same suite (Mac OS 9.0.4) on MAME's PowerPC core, native back-end.
add_test(
  NAME guest_suite_ppc_mame
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --core mame-ppc --jit --timeout 120 --port 18105
)
set_tests_properties(guest_suite_ppc_mame PROPERTIES
  TIMEOUT 160
  LABELS "guest;mame"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)



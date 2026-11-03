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
  ${CMAKE_SOURCE_DIR}/src/common/platform_glue.cpp   # g_platform (vclock reads through it)
)
target_include_directories(test_cpu_cores PRIVATE ${CMAKE_SOURCE_DIR}/src/cpu/core)
target_link_libraries(test_cpu_cores PRIVATE mame_m68k mame_ppc)
target_compile_features(test_cpu_cores PRIVATE cxx_std_17)
add_test(NAME cpu_cores COMMAND test_cpu_cores)
set_tests_properties(cpu_cores PROPERTIES TIMEOUT 10 LABELS "unit;mame")

# Executor's API definitions as the Multiversal Interfaces for Retro68:
# generate the C headers and glue library, build a program against them.
# Skips (77) without Retro68's m68k compiler.
find_program(RETRO68_PPC_GCC powerpc-apple-macos-gcc
  HINTS "$ENV{HOME}/opt/retro68/build-ppc/toolchain/bin")
add_test(
  NAME multiversal_retro68
  COMMAND bash ${TEST_DIR}/test_multiversal_retro68.sh
          $<$<BOOL:${RETRO68_GCC}>:--gcc> $<$<BOOL:${RETRO68_GCC}>:${RETRO68_GCC}>
          $<$<BOOL:${RETRO68_PPC_GCC}>:--ppc-gcc> $<$<BOOL:${RETRO68_PPC_GCC}>:${RETRO68_PPC_GCC}>
)
set_tests_properties(multiversal_retro68 PROPERTIES
  TIMEOUT 120
  LABELS "unit;executor"
  SKIP_RETURN_CODE 77
)

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
  COMMAND bash ${TEST_DIR}/test_config_api.sh 18178
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
    COMMAND ${CMAKE_SOURCE_DIR}/tests/test_boot_se.sh --timeout 30 --port 18180
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
  COMMAND bash ${TEST_DIR}/test_boot_to_finder.sh --backend uae --core mame-68k --timeout 25 --port 18170
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
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --core mame-ppc --timeout 45 --port 18172
)
set_tests_properties(boot_ppc_mame PROPERTIES
  TIMEOUT 65
  LABELS "boot;mame"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_mame_jit
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --core mame-ppc --jit --timeout 30 --port 18176 --webserver
)
set_tests_properties(boot_ppc_mame_jit PROPERTIES
  TIMEOUT 50
  LABELS "boot;mame"
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

add_test(
  NAME boot_ppc_mame_api
  COMMAND bash ${TEST_DIR}/test_boot_ppc.sh --core mame-ppc --timeout 60 --port 18174 --webserver
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
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --timeout 60 --port 18182
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

# PowerPC on Executor: a PowerPC-only application (tests/guest/ppc-hello, on
# InterfaceLib alone) runs on the mame-ppc core behind Executor's PowerCore
# facade.
add_test(
  NAME ppc_hello_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ppc-hello/PPCHello.bin --results ppc_hello.txt --network none --timeout 150 --port 18440
)
set_tests_properties(ppc_hello_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# StdCLib and MathLib on Executor (docs/executor/PPCLibs.md): CLibTest
# (tests/guest/clib, MrC + PPCLink on StdCLib and MathLib) against Apple's
# libraries on the Power Mac (Mac OS 9.0.4 on mame-ppc), the reference.
add_test(
  NAME clib_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/clib/CLibTest.bin --results clib_results.txt --network none --timeout 150 --port 18442
)
set_tests_properties(clib_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME clib_rom_ppc
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --core mame-ppc --jit --app ${CMAKE_SOURCE_DIR}/tests/guest/clib/CLibTest.bin --results clib_results.txt --network none --timeout 150 --port 18452
)
set_tests_properties(clib_rom_ppc PROPERTIES
  TIMEOUT 220
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

# StdCLib's device table and descriptor layer (FSYS, CONS, SYST, a device of
# the program's own in slot 3; MakeResolvedPath) by DevTest
# (tests/guest/devtest, MPW): Executor's, and Apple's on Mac OS 9.0.4.
add_test(
  NAME devtest_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/devtest/DevTest.bin --results devtest_results.txt --network none --timeout 120 --port 18458
)
set_tests_properties(devtest_executor PROPERTIES
  TIMEOUT 180
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME devtest_rom_ppc
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --core mame-ppc --jit --app ${CMAKE_SOURCE_DIR}/tests/guest/devtest/DevTest.bin --results devtest_results.txt --network none --timeout 150 --port 18478
)
set_tests_properties(devtest_rom_ppc PROPERTIES
  TIMEOUT 220
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
)

# PowerPC breakpoints and traces in an Executor session (--executor-ppc-break,
# /api/executor/ppc-break; docs/executor/PPCTrace.md): CLibTest, a routine
# breakpoint from the command line and a fragment one over the API.
add_test(
  NAME ppc_break_executor
  COMMAND bash ${TEST_DIR}/test_executor_ppc_break.sh $<TARGET_FILE:mac-phoenix> 18472
)
set_tests_properties(ppc_break_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The same for CFM-68K (CLibTest68K: SC -model cfmflat + ILink on NuStdCLib
# and NuIntEnv, StdCLib's CFM-68K halves; MathLib's checks are PowerPC-only):
# Executor's StdCLib, and Apple's NuStdCLib on the Quadra with the CFM-68K
# Runtime Enabler (dev-7.6.1), the reference.
add_test(
  NAME clib_cfm68k_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/clib/CLibTest68K.bin --results clib_results.txt --network none --timeout 150 --port 18446
)
set_tests_properties(clib_cfm68k_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME clib_cfm68k
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --image dev-7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/clib/CLibTest68K.bin --results clib_results.txt --network none --timeout 200 --port 18448
)
set_tests_properties(clib_cfm68k PROPERTIES
  TIMEOUT 300
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

# RetroTest (tests/guest/retro68, build.sh): built with Retro68 against the
# Multiversal Interfaces generated from Executor's definitions, for 68k and
# PowerPC (traps, Component Manager and OSA inlines; InterfaceLib and
# AppleScriptLib imports). On Executor, and on the ROM machines (the
# Quadra with 7.5.5, Mac OS 9.0.4 on mame-ppc) as the reference.
foreach(cell IN ITEMS "retro68_executor;--backend executor;RetroTest68K;18770;executor"
                      "retro68_ppc_executor;--backend executor;RetroTestPPC;18772;executor"
                      "retro68_rom;;RetroTest68K;18774;"
                      "retro68_rom_ppc;--backend kpx --core mame-ppc --jit;RetroTestPPC;18776;")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 app)
  list(GET cell 3 port)
  list(GET cell 4 label)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --app ${CMAKE_SOURCE_DIR}/tests/guest/retro68/${app}.bin --results retro68_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 220
    LABELS "guest;${label}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=$<IF:$<STREQUAL:${app},RetroTestPPC>,${TEST_PPC_ROM},${TEST_ROM}>"
  )
endforeach()

# ... and its PowerPC build on Executor's own AppleScriptLib exports (the
# definitions' OSA calls), Apple's file deleted from the run's System.
add_test(
  NAME retro68_ppc_executor_asl
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/retro68/RetroTestPPC.bin --results retro68_results.txt --network none --timeout 150 --port 18778 --delete-file "System Folder:Extensions:AppleScriptLib"
)
set_tests_properties(retro68_ppc_executor_asl PROPERTIES
  TIMEOUT 220
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# MacPhoenixSuite built with Retro68 on the Multiversal Interfaces alone
# (tests/guest/suite/retro68/build.sh; the MPW build uses Apple's): the
# same results expected as from the MPW build, 68k and PowerPC, on Apple's
# systems and on Executor.
foreach(cell IN ITEMS "guest_suite_retro68;;MacPhoenixSuite68K;18801;"
                      "guest_suite_retro68_executor;--backend executor --network none;MacPhoenixSuite68K;18803;executor"
                      "guest_suite_retro68_ppc;--backend kpx --core mame-ppc --jit;MacPhoenixSuitePPC;18805;"
                      "guest_suite_retro68_ppc_executor;--backend executor --network none;MacPhoenixSuitePPC;18807;executor")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 app)
  list(GET cell 3 port)
  list(GET cell 4 label)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --app ${CMAKE_SOURCE_DIR}/tests/guest/suite/retro68/${app}.bin --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 220
    LABELS "guest;${label}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=$<IF:$<STREQUAL:${app},MacPhoenixSuitePPC>,${TEST_PPC_ROM},${TEST_ROM}>"
  )
endforeach()

# Retro68's own AutomatedTests (68k and PowerPC) on Executor, through
# LaunchAPPL's Executor backend (tools/retro68/launchappl-executor); skipped
# without the Retro68 build at ~/opt/retro68/build-ppc.
add_test(
  NAME retro68_automated_executor
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/retro68/automated_tests.py
)
set_tests_properties(retro68_automated_executor PROPERTIES
  TIMEOUT 600
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACPHOENIX_EMULATOR=$<TARGET_FILE:mac-phoenix>"
)

# The fat MacPhoenixSuite's PowerPC code on Executor (--executor-ppc):
# StdCLib, AppleScriptLib through CallComponentUPP, exit() through StdCLib.
add_test(
  NAME guest_suite_ppc_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --executor-ppc --network none --timeout 150 --port 18454
)
set_tests_properties(guest_suite_ppc_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The Notification Manager from an application in the background: the alert
# with its text, the response procedure after the alert is dismissed (the
# host presses Return), nmTypErr: tests/guest/notify. The ROM machine is the
# reference.
add_test(
  NAME notify_guest
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --app ${CMAKE_SOURCE_DIR}/tests/guest/notify/NotifyTest.bin --results notify_test.txt --key-on waits_for_alert:36 --network none --timeout 90 --port 18430
)
set_tests_properties(notify_guest PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME notify_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/notify/NotifyTest.bin --results notify_test.txt --key-on waits_for_alert:36 --network none --timeout 150 --port 18432
)
set_tests_properties(notify_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# SizeResource and MaxSizeRsrc for resources loaded and not (a loaded one
# answers its handle's size: ClarisWorks 4's launch check): tests/guest/ressize.
# The ROM machine is the reference.
add_test(
  NAME ressize_guest
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/ressize/ResSizeTest.bin --results ressize_test.txt --network none --timeout 90 --port 18460
)
set_tests_properties(ressize_guest PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME ressize_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/ressize/ResSizeTest.bin --results ressize_test.txt --network none --timeout 150 --port 18462
)
set_tests_properties(ressize_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# GetItemStyle through the trap writes a word, the style in its low byte
# (AppleWorks 5's command keys): tests/guest/itemstyle. The ROM machine is
# the reference.
add_test(
  NAME itemstyle_guest
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/itemstyle/ItemStyleTest.bin --results itemstyle_test.txt --network none --timeout 90 --port 18464
)
set_tests_properties(itemstyle_guest PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME itemstyle_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/itemstyle/ItemStyleTest.bin --results itemstyle_test.txt --network none --timeout 150 --port 18466
)
set_tests_properties(itemstyle_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# LNextCell always leaves the advanced cell, h back to 0 past a row's end,
# with or without a next one (AppleWorks 5's Define Database Fields counts
# selected cells until LGetSelect finds none): tests/guest/listcell. The ROM
# machine is the reference.
add_test(
  NAME listcell_guest
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/listcell/ListCellTest.bin --results listcell_test.txt --network none --timeout 90 --port 18468
)
set_tests_properties(listcell_guest PROPERTIES
  TIMEOUT 130
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

add_test(
  NAME listcell_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/listcell/ListCellTest.bin --results listcell_test.txt --network none --timeout 150 --port 18470
)
set_tests_properties(listcell_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# provisioning/bake.py's serial steps, with a fake serials file: typed, never
# logged, annotated or keyed; a missing one stops the bake (no emulator).
add_test(
  NAME bake_serials
  COMMAND ${Python3_EXECUTABLE} ${TEST_DIR}/test_bake_serials.py
)
set_tests_properties(bake_serials PROPERTIES TIMEOUT 60 LABELS "provisioning")

# A bake's "exclude" paths ("Folder:Name", a serial number file an archive came
# with) left out of the snapshot and the Installers disk (no emulator).
add_test(
  NAME bake_exclude
  COMMAND ${Python3_EXECUTABLE} ${TEST_DIR}/test_bake_exclude.py
)
set_tests_properties(bake_exclude PROPERTIES TIMEOUT 60 LABELS "provisioning")

# build.py's "printer": the Chooser's STR and 'alis' -8192 as a 7.6.1 Quadra
# writes them, inherited from a built base, not in its content identity.
add_test(
  NAME build_printer
  COMMAND ${Python3_EXECUTABLE} ${TEST_DIR}/test_build_printer.py
)
set_tests_properties(build_printer PROPERTIES TIMEOUT 120 LABELS "provisioning")

# AppleWorks 5.0.3 on Executor's 7.6.1: a word-processing document made,
# saved on the Host volume, opened again and saved (tests/test_appleworks.py),
# with its 68k code and with its PowerPC code.
set(_aw_68k_args)
set(_aw_ppc_args --ppc)
foreach(_aw IN ITEMS 68k ppc)
  add_test(
    NAME appleworks_executor_761_${_aw}
    COMMAND ${Python3_EXECUTABLE} ${TEST_DIR}/test_appleworks.py --system 7.6.1 ${_aw_${_aw}_args}
  )
  set_tests_properties(appleworks_executor_761_${_aw} PROPERTIES
    TIMEOUT 400
    LABELS "guest;executor"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACPHOENIX_EMULATOR=$<TARGET_FILE:mac-phoenix>"
  )
endforeach()

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

# A DOS disk (a FAT16 image) through PC Exchange on Executor's File System
# Manager: read off it, written onto it, consistent after shutdown (7.6.1;
# 7.5.5's PC Exchange 2.0.5 is off, STRAGGLERS 62).
add_test(
  NAME pcexchange_executor
  COMMAND bash ${TEST_DIR}/test_pcexchange_executor.sh --timeout 90 --port 18450
)
set_tests_properties(pcexchange_executor PROPERTIES
  TIMEOUT 160
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# --- Executor variants: Apple's 7.5.5 System on Executor's Toolbox, no ROM.
# Runs on the System Folder of the test copy of tests-7.5.5.img (extracted
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

# The Quit Apple event (/api/quit) to an application on Executor, its 68k
# code and its PowerPC code (--executor-ppc): SimpleSound on 7.6.1, whose
# Apple Event Manager is AppleScript's PACK 8; PowerPC code reaches it
# through Pack8's patched table entry, as InterfaceLib does on a Mac.
foreach(cell IN ITEMS "command_bridge_quit_executor_761;;18964"
                      "command_bridge_quit_ppc_executor_761;--executor-ppc;18966")
  list(GET cell 0 name)
  list(GET cell 1 flags)
  list(GET cell 2 port)
  separate_arguments(flag_args UNIX_COMMAND "${flags}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_command_bridge.sh --backend executor --os-version 7.6.1 ${flag_args} --quit-app "DISK:System Folder:Apple Menu Items:SimpleSound" SimpleSound --timeout 60 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 120
    LABELS "executor;761"
    SKIP_RETURN_CODE 77
  )
endforeach()

add_test(
  NAME guest_suite_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --timeout 150 --port 18112
)
set_tests_properties(guest_suite_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# CFM-68K (docs/executor/CFM68K.md): the suite built as a CFM-68K application
# (build.cw.mpw's MacPhoenixSuiteCFM68K: code in the data fork, MSL linked
# in, InterfaceLib and AppleScriptLib imported) on Executor's own Code
# Fragment Manager; the ROM machine with Apple's CFM-68K Runtime Enabler
# (dev-7.6.1) is the reference, without the network (its OT has none here).
add_test(
  NAME guest_suite_cfm68k_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/suite/MacPhoenixSuiteCFM68K.bin --results cfm68k_results.txt --timeout 150 --port 18712
)
set_tests_properties(guest_suite_cfm68k_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME guest_suite_cfm68k
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --image dev-7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/suite/MacPhoenixSuiteCFM68K.bin --results cfm68k_results.txt --sections "disk extfs audio applescript" --network none --timeout 200 --port 18714
)
set_tests_properties(guest_suite_cfm68k PROPERTIES
  TIMEOUT 300
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# MacTCP in depth (MacPhoenixSuite's mactcp section, after network): async
# calls, ASRs, no-copy receive, passive open, timeouts, UDP, TCPGlobalInfo,
# the resolver's other calls. On Executor against a host echo server; on
# the ROM machine (Open Transport's MacTCP, net-bridge's echo) as the
# reference: what OT's MacTCP doesn't do there is a skip.
add_test(
  NAME mactcp_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --sections "network mactcp" --results mactcp_results.txt --network none --timeout 150 --port 18530
)
set_tests_properties(mactcp_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME mactcp_rom
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --sections "network mactcp" --results mactcp_results.txt --timeout 120 --port 18534
)
set_tests_properties(mactcp_rom PROPERTIES
  TIMEOUT 160
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)
add_test(
  NAME mactcp_rom_ppc
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --sections "network mactcp" --results mactcp_results.txt --timeout 150 --port 18536
)
set_tests_properties(mactcp_rom_ppc PROPERTIES
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

add_test(NAME boot_iici
  COMMAND bash ${TEST_DIR}/test_boot_iici.sh --timeout 40 --port 18420)
set_tests_properties(boot_iici PROPERTIES TIMEOUT 70 LABELS "boot" SKIP_RETURN_CODE 77)

add_test(NAME boot_iici_mame68k
  COMMAND bash ${TEST_DIR}/test_boot_iici.sh --core mame-68k --timeout 40 --port 18422)
set_tests_properties(boot_iici_mame68k PROPERTIES TIMEOUT 70 LABELS "boot;mame" SKIP_RETURN_CODE 77)

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

# The Executor backend's cursor is the page's (client/cursor.js), on both
# transports. Headless Chromium through playwright: PLAYWRIGHT_NODE_PATH, a
# node_modules holding it (and PLAYWRIGHT_CHROMIUM, a browser other than its
# own); skips without.
set(PLAYWRIGHT_NODE_PATH "" CACHE PATH "node_modules with playwright, for the page tests")
set(PLAYWRIGHT_CHROMIUM "" CACHE FILEPATH "Chromium for the page tests (default: playwright's)")
foreach(codec png h264)
  if(codec STREQUAL "png")
    set(port 18444)
  else()
    set(port 18456)
  endif()
  add_test(NAME cursor_executor_${codec}
    COMMAND bash ${TEST_DIR}/test_cursor_executor.sh --codec ${codec} --port ${port})
  set_tests_properties(cursor_executor_${codec} PROPERTIES
    TIMEOUT 120 LABELS "executor" SKIP_RETURN_CODE 77
    ENVIRONMENT "NODE_PATH=${PLAYWRIGHT_NODE_PATH};CHROMIUM=${PLAYWRIGHT_CHROMIUM}")
endforeach()

# Executor's HiDPI chrome runs (docs/executor/HiDPI.md "Phase 2"): ChromeGallery
# on 8.6 at --executor-scale 2, the 2x screen against the 1x frame scaled up
# inside known pieces of chrome. Skips without Skia (-DEXECUTOR_SKIA=ON).
add_test(NAME hidpi_chrome_executor
  COMMAND bash ${TEST_DIR}/test_hidpi_chrome.sh --port 18474)
set_tests_properties(hidpi_chrome_executor PROPERTIES
  TIMEOUT 180 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# HiDPI text runs behind windows: SimpleText's lines under its "Save changes"
# alert (no 2x slivers in the alert), the alert's pressed Cancel after it closed
# (no grey box left on the document's white).
add_test(NAME hidpi_windows_executor
  COMMAND bash ${TEST_DIR}/test_hidpi_windows.sh --port 18620)
set_tests_properties(hidpi_windows_executor PROPERTIES
  TIMEOUT 180 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# The 8.6 Finder's desktop labels after an update that covers only part of
# them: a Finder window over half of two labels closed, the icon column as
# before (Executor's DMDrawDesktopRegion clipped like the Finder's update).
add_test(NAME desktop_labels_executor
  COMMAND bash ${TEST_DIR}/test_desktop_labels_executor.sh --port 18516)
set_tests_properties(desktop_labels_executor PROPERTIES
  TIMEOUT 150 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# 8.6's platinum chrome where the G3 was measured: the menu bar's apple,
# a Finder window's scroll bar thumb, the disks' icons, a short click paging once.
add_test(NAME platinum_86_executor
  COMMAND bash ${TEST_DIR}/test_platinum_86_executor.sh --port 18518)
set_tests_properties(platinum_86_executor PROPERTIES
  TIMEOUT 200 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# Two sessions on one --executor-system at once: the second runs on a copy
# of its own (executor-systems/run/<pid>), both keep their BridgeAgent.
add_test(NAME system_sessions_executor
  COMMAND bash ${TEST_DIR}/test_executor_system_sessions.sh --port 18540)
set_tests_properties(system_sessions_executor PROPERTIES
  TIMEOUT 180 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# A PowerPC application launched a second time (BBEdit Lite 6.1 on 8.6,
# then an odoc): the System's accelerated WDEF, prepared by the first, is the
# system's, not the first launch's heap.
add_test(NAME relaunch_ppc_executor
  COMMAND bash ${TEST_DIR}/test_relaunch_ppc_executor.sh --port 18542)
set_tests_properties(relaunch_ppc_executor PROPERTIES
  TIMEOUT 240 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# 8.6 at 2x: the desktop's labels sharp again after a Finder window has
# been over part of them (their runs outlive being covered in part).
add_test(NAME hidpi_labels_executor
  COMMAND bash ${TEST_DIR}/test_hidpi_labels_executor.sh --port 18544)
set_tests_properties(hidpi_labels_executor PROPERTIES
  TIMEOUT 240 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# An alert left up idles the host and OK still answers: the 8.6 Finder's
# Erase Disk alert on the read-only image, the emulator's CPU time while it
# is up (Executor's ModalDialog waits a tick between events).
add_test(NAME modal_idle_executor
  COMMAND bash ${TEST_DIR}/test_modal_idle_executor.sh --port 18531)
set_tests_properties(modal_idle_executor PROPERTIES
  TIMEOUT 150 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# 8.x selects text in the highlight colour: SimpleText's selection in the
# Mac OS Default theme's lavender with no colour saved in parameter RAM,
# not inverted black.
add_test(NAME text_hilite_executor
  COMMAND bash ${TEST_DIR}/test_text_hilite_executor.sh --port 18532)
set_tests_properties(text_hilite_executor PROPERTIES
  TIMEOUT 150 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# Executor's HiDPI GWorld shadows (docs/executor/HiDPI.md "Phase 4"): GWorldGallery
# on 8.6 at --executor-scale 2, text drawn offscreen and copied to the screen
# against the 1x frame scaled up. Skips without Skia (-DEXECUTOR_SKIA=ON).
add_test(NAME hidpi_gworld_executor
  COMMAND bash ${TEST_DIR}/test_hidpi_gworld.sh --port 18476)
set_tests_properties(hidpi_gworld_executor PROPERTIES
  TIMEOUT 180 LABELS "executor" SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_BINARY=$<TARGET_FILE:mac-phoenix>")

# Executor's Toolbox gtests built with Retro68 into a 68k app and run in
# the guest: on Apple's ROM and System (the ground truth) and on Executor.
# Skips without Retro68 (toolchain/retro68). The cells share the app's
# build directory (build-m68k-tests), hence the lock.
foreach(cell uae executor)
  if(cell STREQUAL "uae")
    set(args --backend uae --port 18410)
    set(labels "guest")
  else()
    set(args --backend executor --port 18412)
    set(labels "guest;executor")
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
# MPW tool run in ToolServer). Skips without the MPW disk.
add_test(
  NAME ot_executor
  COMMAND bash ${TEST_DIR}/test_ot_executor.sh --port 18394
)
set_tests_properties(ot_executor PROPERTIES
  TIMEOUT 300
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
# ... and without Apple's Shared Library Manager: OT's glue binds through
# Executor's own.
add_test(
  NAME ot_executor_noaslm
  COMMAND bash ${TEST_DIR}/test_ot_executor.sh --no-aslm --port 18400
)
set_tests_properties(ot_executor_noaslm PROPERTIES
  TIMEOUT 300
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The same cases from PowerPC code: OTTestPPC (MrC + PPCLink on Apple's
# PowerPC OT glue) imports OTClientLib & co., which Executor answers from
# the same routines (OpenTransport.yaml); results in Host:ot_results.txt.
add_test(
  NAME ot_ppc_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestPPC.bin --results ot_results.txt --timeout 150 --port 18738
)
set_tests_properties(ot_ppc_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# And built with Retro68 against the headers generated from the definitions
# (OpenTransport.yaml, OpenTptInternet.yaml), its public calls the glue in
# custom/OpenTransportGlue.c (tests/guest/ot/build-retro68.sh).
add_test(
  NAME ot_retro68_ppc_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetroPPC.bin --results ot_results.txt --timeout 150 --port 18740
)
set_tests_properties(ot_retro68_ppc_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The reference: Apple's Open Transport 2.6.1 (Mac OS 9.0.4) on the G3
# under mame-ppc, against net-bridge's echo service (--echo-gateway), with
# our glue (OTTestRetroPPC). OTTest's cases are what Apple's OT does (1.1.1
# on 7.6.1 too, by hand: STRAGGLERS 66).
add_test(
  NAME ot_retro68_ppc_rom
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend kpx --core mame-ppc --jit --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetroPPC.bin --results ot_results.txt --echo-gateway --timeout 220 --port 18744
)
set_tests_properties(ot_retro68_ppc_rom PROPERTIES
  TIMEOUT 300
  LABELS "guest"
  SKIP_RETURN_CODE 77
)

# OTTest for 68k from the same headers, its calls bound through the Shared
# Library Manager by the generated glue (TLibraryManager::GetFunctionPointer):
# on Executor, and on the Quadra with Mac OS 7.6.1, Apple's ASLM and OT 1.1.1.
add_test(
  NAME ot_retro68_68k_executor
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetro68K.bin --results ot_results.txt --timeout 150 --port 18746
)
set_tests_properties(ot_retro68_68k_executor PROPERTIES
  TIMEOUT 200
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)
add_test(
  NAME ot_retro68_68k_rom
  COMMAND bash ${TEST_DIR}/test_guest_suite.sh --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetro68K.bin --results ot_results.txt --echo-gateway --timeout 200 --port 18748
)
set_tests_properties(ot_retro68_68k_rom PROPERTIES
  TIMEOUT 280
  LABELS "guest"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MACEMU_ROM=${TEST_ROM}"
)

# Open Transport's configuration database (OTLib$CfigLib on 68k,
# OTConfigLib on PowerPC: what the TCP/IP control panel reads and writes;
# ot_config.cpp) called directly by CfgTest (tests/guest/otcfg, Retro68):
# Apple's OT 1.1.1 with Mac OS 7.6.1 on the Quadra and the G3 is the
# reference, Executor on Apple's 7.6.1 System.
foreach(cell IN ITEMS "otcfg_rom;;CfgTest68K;18952;guest;761"
                      "otcfg_rom_ppc;--backend kpx --core mame-ppc --jit;CfgTestPPC;18956;guest;761;mame"
                      "otcfg_executor;--backend executor;CfgTest68K;18954;guest;761;executor"
                      "otcfg_executor_ppc;--backend executor;CfgTestPPC;18958;guest;761;executor")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 app)
  list(GET cell 3 port)
  list(SUBLIST cell 4 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/otcfg/${app}.bin --results cfg_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=$<IF:$<STREQUAL:${app},CfgTestPPC>,${TEST_PPC_ROM},${TEST_ROM}>"
  )
endforeach()

# The Shared Library Manager's PowerPC side (what the TCP/IP control
# panel's PowerPC code uses; ot_slm_ppc.cpp) through the ASLM SDK's glue:
# SLMTest (tests/guest/slm, MPW MrCpp). Apple's "Shared Library Manager
# PPC" with Mac OS 7.6.1 on the G3 is the reference.
foreach(cell IN ITEMS "slm_rom_ppc;--backend kpx --core mame-ppc --jit;18960;guest;761;mame"
                      "slm_executor_ppc;--backend executor;18962;guest;761;executor")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 port)
  list(SUBLIST cell 3 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/slm/SLMTest.bin --results slm_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# The Drag Manager from PowerPC code (DragLib) by DragTest (tests/guest/drag,
# Retro68): on Executor, glue through DragDispatch's table entry to Apple's
# 68k Drag Manager (TRAP_GLUE); Apple's own on the G3, 7.6.1 and 9.0.4.
foreach(cell IN ITEMS "drag_ppc_executor_761;--backend executor;7.6.1;18976;guest;761;executor"
                      "drag_rom_ppc_761;--backend kpx --core mame-ppc --jit;7.6.1;18978;guest;761;mame"
                      "drag_rom_ppc_904;--backend kpx --core mame-ppc --jit;9.0.4;18980;guest;mame")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 os)
  list(GET cell 3 port)
  list(SUBLIST cell 4 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --os-version ${os} --app ${CMAKE_SOURCE_DIR}/tests/guest/drag/DragTestPPC.bin --results drag_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# The Thread Manager from PowerPC code (ThreadsLib) by ThreadTest
# (tests/guest/threads, Retro68): on Executor, threads whose entries and
# procedures are PowerPC code, switched while inside it (the core's
# execution context per thread); Apple's on the G3.
foreach(cell IN ITEMS "threads_ppc_executor_761;--backend executor;18982;guest;761;executor"
                      "threads_rom_ppc_761;--backend kpx --core mame-ppc --jit;18984;guest;761;mame")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 port)
  list(SUBLIST cell 3 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --os-version 7.6.1 --app ${CMAKE_SOURCE_DIR}/tests/guest/threads/ThreadTestPPC.bin --results thread_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# The Collection Manager from PowerPC code (CollectionsLib) by
# CollectionTest (tests/guest/collections, Retro68): Executor's
# (collections.cpp), and Apple's on the G3 with 9.0.4 (7.6.1 has none).
foreach(cell IN ITEMS "collections_ppc_executor_761;--backend executor;7.6.1;18986;guest;761;executor"
                      "collections_rom_ppc_904;--backend kpx --core mame-ppc --jit;9.0.4;18988;guest;mame")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 os)
  list(GET cell 3 port)
  list(SUBLIST cell 4 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --os-version ${os} --app ${CMAKE_SOURCE_DIR}/tests/guest/collections/CollectionTestPPC.bin --results collection_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# Apple Menu Options' submenus on 8.6 (its INIT, in an 'APPC' control
# panel): the Apple menu held down on Control Panels shows a submenu. The
# G3 the reference, Executor on 8.6's System.
foreach(cell IN ITEMS "applemenu_submenus_executor_86;--backend executor;18184;guest;executor"
                      "applemenu_submenus_rom_ppc_86;--backend kpx --core mame-ppc --jit;18186;guest;mame")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 port)
  list(SUBLIST cell 3 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_apple_menu_submenus.sh ${backend_args} --timeout 120 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 180
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# Scroll bars on 8.6: the Finder's live scrolling (a thumb dragged scrolls
# the window before the button is let go), and on Executor Standard File's
# list paging both ways (AppleWorks' Open). The G3 the reference.
foreach(cell IN ITEMS "scroll_bars_executor_86;--backend executor;18210;guest;executor"
                      "scroll_bars_rom_ppc_86;--backend kpx --core mame-ppc --jit;18212;guest;mame")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 port)
  list(SUBLIST cell 3 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_scroll_bars_86.sh ${backend_args} --timeout 120 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 240
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# CodeWarrior IDE 3.2 (PowerPC) on Executor on 8.6's System: runs, its
# second preparation of Translation (a global data section) given the
# system's instance, relocated, not freed memory.
add_test(
  NAME codewarrior_executor_86
  COMMAND bash ${TEST_DIR}/test_codewarrior_86.sh --timeout 120 --port 18340
)
set_tests_properties(codewarrior_executor_86 PROPERTIES
  TIMEOUT 240
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# THINK C 6 on Executor on 7.6.1's System: Project > Run launches the
# project through MultiFinder's short 'LC' block, and its runtime stub finds
# THINK's data in the ApplScratch it starts with.
add_test(
  NAME thinkc_run_executor
  COMMAND bash ${TEST_DIR}/test_thinkc_run_executor.sh --timeout 150 --port 18780
)
set_tests_properties(thinkc_run_executor PROPERTIES
  TIMEOUT 300
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# The Utilities disk (system information and benchmarks) on Executor:
# Gestalt.Appl on 7.6.1; Gestalt.Appl, TattleTech and MacBench 5.0 on 8.6.
foreach(_us IN ITEMS 7.6.1 8.6)
  add_test(
    NAME utilities_executor_${_us}
    COMMAND bash ${TEST_DIR}/test_utilities_executor.sh ${_us} --port 18790
  )
  set_tests_properties(utilities_executor_${_us} PROPERTIES
    TIMEOUT 400
    LABELS "guest;executor"
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK utilities_executor
  )
endforeach()

# Executor's Gestalt against the Macs' (tests/guest/gestalt: the probe and
# the answers measured on the Quadra 650 and the G3): identity and managers.
foreach(_gc IN ITEMS 7.6.1 7.6.1-ppc 8.6)
  add_test(
    NAME gestalt_executor_${_gc}
    COMMAND bash ${TEST_DIR}/test_gestalt_executor.sh ${_gc} --port 18874
  )
  set_tests_properties(gestalt_executor_${_gc} PROPERTIES
    TIMEOUT 300
    LABELS "guest;executor"
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK gestalt_executor
  )
endforeach()

# MPW Shell (PowerPC) on Executor with 8.6's System: Startup builds the
# Project, Directory and Build menus (commands found through StdCLib's
# MakeResolvedPath), a do-script's output redirected to a file.
add_test(
  NAME mpw_executor_86
  COMMAND bash ${TEST_DIR}/test_mpw_86.sh --timeout 120 --port 18344
)
set_tests_properties(mpw_executor_86 PROPERTIES
  TIMEOUT 240
  LABELS "guest;executor"
  SKIP_RETURN_CODE 77
)

# Icon Services from PowerPC code (IconServicesLib) by IconTest
# (tests/guest/iconservices, Retro68): Executor's (iconservices.cpp), and
# Apple's on the G3 with 9.0.4 and 8.6; on 8.6 also Executor's with 8.6's System.
foreach(cell IN ITEMS "icons_ppc_executor_761;--backend executor;--os-version 7.6.1;18990;guest;761;executor"
                      "icons_rom_ppc_904;--backend kpx --core mame-ppc --jit;--os-version 9.0.4;18992;guest;mame"
                      "icons_ppc_executor_86;--backend executor --executor-ppc;--image 8.6;18994;guest;executor"
                      "icons_rom_ppc_86;--backend kpx --core mame-ppc --jit;--image 8.6;18996;guest;mame")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 os)
  list(GET cell 3 port)
  list(SUBLIST cell 4 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  separate_arguments(os_args UNIX_COMMAND "${os}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} ${os_args} --app ${CMAKE_SOURCE_DIR}/tests/guest/iconservices/IconTestPPC.bin --results icon_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# An application's partition (SIZE 100K: preferred + 56K on 8.6, the stack
# DefltStack + 16K) as the Process Manager lays it out: tests/guest/partprobe,
# 8.6 on the G3 the reference, Executor on 8.6's System
# (process_layout_partition). The _default cell leaves out --executor-ppc:
# on an 8.x System Executor runs fat applications' PowerPC code by itself
# (version.cpp), the Finder's first of all. The _fragment cells run
# PartProbePPC: a PowerPC application's partition grows by its fragment's
# length (+ 28K on 7.6.1, + 50K on 8.6), the fragment loaded into its heap.
foreach(cell "partition_rom_ppc_86;--backend kpx --core mame-ppc --jit;8.6;PartProbe;18490;guest;mame"
             "partition_executor_86;--backend executor --executor-ppc;8.6;PartProbe;18492;guest;executor"
             "partition_executor_86_default;--backend executor;8.6;PartProbe;18494;guest;executor"
             "partition_fragment_rom_ppc_86;--backend kpx --core mame-ppc --jit;8.6;PartProbePPC;18496;guest;mame"
             "partition_fragment_executor_86;--backend executor --executor-ppc;8.6;PartProbePPC;18498;guest;executor"
             "partition_fragment_rom_ppc_761;--backend kpx --core mame-ppc --jit;7.6.1;PartProbePPC;18500;guest;mame"
             "partition_fragment_executor_761;--backend executor --executor-ppc;7.6.1;PartProbePPC;18502;guest;executor")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 image)
  list(GET cell 3 app)
  list(GET cell 4 port)
  list(SUBLIST cell 5 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --image ${image} --app ${CMAKE_SOURCE_DIR}/tests/guest/partprobe/${app}.bin --results part_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# InterfaceLib's lowercase C routines (numtostring, getindstring, ptinrect,
# sfputfile, ...; `cglue:` in the definitions) against their Pascal
# routines: tests/guest/cglueprobe, 8.6 on the G3 the reference. Return
# answers sfputfile's dialog.
foreach(cell "cglue_rom_ppc_86;--backend kpx --core mame-ppc --jit;18504;mame"
             "cglue_executor_86;--backend executor --executor-ppc;18506;executor")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 port)
  list(GET cell 3 label)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} --image 8.6 --app ${CMAKE_SOURCE_DIR}/tests/guest/cglueprobe/CGlueProbePPC.bin --results cglue_results.txt --key-on "WAITING sfputfile:36" --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "guest;${label}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}"
  )
endforeach()

# GetIndString and GetIndPattern given an index outside the list, an empty
# list, a missing one: tests/guest/strprobe. The Macs are the reference
# (the Quadra with 7.6.1 through Apple's 68k glue, the G3 with 8.6 through
# InterfaceLib; the same answers), Executor's own from PowerPC code.
foreach(cell "strprobe_rom_761;;StrProbe.bin;--os-version 7.6.1;${TEST_ROM};18560;guest"
             "strprobe_rom_ppc_86;--backend kpx --core mame-ppc --jit;StrProbePPC.bin;--image 8.6;${TEST_PPC_ROM};18562;guest;mame"
             "strprobe_ppc_executor_86;--backend executor --executor-ppc;StrProbePPC.bin;--image 8.6;;18564;guest;executor"
             "strprobe_ppc_executor_761;--backend executor;StrProbePPC.bin;--os-version 7.6.1;;18566;guest;761;executor")
  list(GET cell 0 name)
  list(GET cell 1 backend)
  list(GET cell 2 app)
  list(GET cell 3 disk)
  list(GET cell 4 rom)
  list(GET cell 5 port)
  list(SUBLIST cell 6 -1 labels)
  separate_arguments(backend_args UNIX_COMMAND "${backend}")
  separate_arguments(disk_args UNIX_COMMAND "${disk}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/test_guest_suite.sh ${backend_args} ${disk_args} --app ${CMAKE_SOURCE_DIR}/tests/guest/strprobe/${app} --results str_results.txt --network none --timeout 150 --port ${port}
  )
  set_tests_properties(${name} PROPERTIES
    TIMEOUT 230
    LABELS "${labels}"
    SKIP_RETURN_CODE 77
    ENVIRONMENT "MACEMU_ROM=${rom}"
  )
endforeach()

# Differential probes (docs/executor/DiffTest.md): each probe's answers on
# Executor (8.6 System) against the committed G3 8.6 reference, known
# differences in tests/guest/difftest/expected/. Fails on a new difference.
foreach(probe aevt appr ctl menu sys text textutil wind)
  add_test(
    NAME difftest_${probe}_executor_86
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/difftest/run.py ${probe} --no-refresh
  )
  # one at a time: run.py's sessions for a target share its port and out
  # dir (another run on it, outside this ctest: waited for, 300 s)
  set_tests_properties(difftest_${probe}_executor_86 PROPERTIES
    TIMEOUT 1200
    LABELS "guest;executor"
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK difftest_executor
    ENVIRONMENT "DIFFTEST_LOCK_TIMEOUT=300"
  )
endforeach()

# Thread Manager through Frontier's UserTalk threads (process.cpp).
add_test(
  NAME threads_executor
  COMMAND bash ${TEST_DIR}/test_threads_executor.sh --timeout 240 --port 18168
)
set_tests_properties(threads_executor PROPERTIES
  TIMEOUT 280
  LABELS "guest;executor"
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

# --- The same cells on Mac OS 7.6.1 (tests-7.6.1.img; ctest -L 761) ---
# On the Quadra, on the G3 (Apple's OT 1.1.1 there crashes about half the
# runs: STRAGGLERS 66) and on Executor. A cell: name; script; its arguments;
# port; the script's timeout; labels (beside guest and 761); the ROM (q: the
# Quadra's, g: the G3's, none: Executor).
set(_ppc "--backend kpx --core mame-ppc --jit")
foreach(cell IN ITEMS
    # The Quadra
    "guest_suite_retro68_761;test_guest_suite.sh;--app ${CMAKE_SOURCE_DIR}/tests/guest/suite/retro68/MacPhoenixSuite68K.bin;18900;150;;q"
    "retro68_rom_761;test_guest_suite.sh;--app ${CMAKE_SOURCE_DIR}/tests/guest/retro68/RetroTest68K.bin --results retro68_results.txt --network none;18902;150;;q"
    "command_bridge_761;test_command_bridge.sh;;18904;40;boot;q"
    "toolbox_suite_uae_761;test_toolbox_suite.sh;--backend uae;18906;180;;q"
    "finder_suite_761;test_guest_suite.sh;--sections finder --results finder_results.txt --network none;18908;120;;q"
    "mactcp_rom_761;test_guest_suite.sh;--sections 'network mactcp' --results mactcp_results.txt;18910;120;;q"
    # The G3 on mame-ppc
    "guest_suite_ppc_761;test_guest_suite.sh;${_ppc};18912;120;mame;g"
    "guest_suite_retro68_ppc_761;test_guest_suite.sh;${_ppc} --app ${CMAKE_SOURCE_DIR}/tests/guest/suite/retro68/MacPhoenixSuitePPC.bin;18914;150;mame;g"
    "retro68_rom_ppc_761;test_guest_suite.sh;${_ppc} --app ${CMAKE_SOURCE_DIR}/tests/guest/retro68/RetroTestPPC.bin --results retro68_results.txt --network none;18916;150;mame;g"
    "ot_retro68_ppc_rom_761;test_guest_suite.sh;${_ppc} --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetroPPC.bin --results ot_results.txt --echo-gateway;18918;220;mame;g"
    "command_bridge_ppc_761;test_command_bridge.sh;${_ppc};18920;40;boot;g"
    # Executor on Apple's 7.6.1 System
    "guest_suite_executor_761;test_guest_suite.sh;--backend executor;18922;150;executor;none"
    "guest_suite_retro68_executor_761;test_guest_suite.sh;--backend executor --network none --app ${CMAKE_SOURCE_DIR}/tests/guest/suite/retro68/MacPhoenixSuite68K.bin;18924;150;executor;none"
    "guest_suite_retro68_ppc_executor_761;test_guest_suite.sh;--backend executor --network none --app ${CMAKE_SOURCE_DIR}/tests/guest/suite/retro68/MacPhoenixSuitePPC.bin;18926;150;executor;none"
    "guest_suite_ppc_executor_761;test_guest_suite.sh;--backend executor --executor-ppc --network none;18928;150;executor;none"
    "retro68_executor_761;test_guest_suite.sh;--backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/retro68/RetroTest68K.bin --results retro68_results.txt --network none;18930;150;executor;none"
    "retro68_ppc_executor_761;test_guest_suite.sh;--backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/retro68/RetroTestPPC.bin --results retro68_results.txt --network none;18932;150;executor;none"
    "ot_executor_761;test_ot_executor.sh;;18934;240;executor;none"
    "ot_ppc_executor_761;test_guest_suite.sh;--backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestPPC.bin --results ot_results.txt;18936;150;executor;none"
    "ot_retro68_ppc_executor_761;test_guest_suite.sh;--backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetroPPC.bin --results ot_results.txt;18938;150;executor;none"
    "ot_retro68_68k_executor_761;test_guest_suite.sh;--backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/ot/OTTestRetro68K.bin --results ot_results.txt;18940;150;executor;none"
    "command_bridge_executor_761;test_command_bridge.sh;--backend executor;18942;60;boot;none"
    "toolbox_suite_executor_761;test_toolbox_suite.sh;--backend executor;18944;180;executor;none"
    "finder_suite_executor_761;test_guest_suite.sh;--backend executor --sections finder --results finder_results.txt --network none;18946;150;executor;none"
    "mactcp_executor_761;test_guest_suite.sh;--backend executor --sections 'network mactcp' --results mactcp_results.txt --network none;18948;150;executor;none"
    "notify_executor_761;test_guest_suite.sh;--backend executor --app ${CMAKE_SOURCE_DIR}/tests/guest/notify/NotifyTest.bin --results notify_test.txt --key-on waits_for_alert:36 --network none;18950;150;executor;none")
  list(GET cell 0 name)
  list(GET cell 1 script)
  list(GET cell 2 args)
  list(GET cell 3 port)
  list(GET cell 4 timeout)
  list(GET cell 5 labels)
  list(GET cell 6 rom)
  separate_arguments(args UNIX_COMMAND "${args}")
  add_test(
    NAME ${name}
    COMMAND bash ${TEST_DIR}/${script} --os-version 7.6.1 ${args} --timeout ${timeout} --port ${port}
  )
  math(EXPR ctest_timeout "${timeout} + 80")
  set_tests_properties(${name} PROPERTIES
    TIMEOUT ${ctest_timeout}
    LABELS "guest;761;${labels}"
    SKIP_RETURN_CODE 77
  )
  if(rom STREQUAL "q")
    set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "MACEMU_ROM=${TEST_ROM}")
  elseif(rom STREQUAL "g")
    set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "MACEMU_ROM=${TEST_PPC_ROM}")
  endif()
  if(script STREQUAL "test_toolbox_suite.sh")
    set_property(TEST ${name} PROPERTY RESOURCE_LOCK toolbox_app)
  endif()
endforeach()
set_property(TEST guest_suite_761 ot_retro68_68k_rom APPEND PROPERTY LABELS 761)
# Apple's OT 1.1.1 on the G3 crashes in OTTest's tcp_async about half the
# runs (STRAGGLERS 66): left in, marked (ctest -LE flaky leaves it out).
set_property(TEST ot_retro68_ppc_rom_761 APPEND PROPERTY LABELS flaky)

# Every test's scratch (logs, ExtFS folders, mktemp dirs) on the SSD, not in
# /tmp: /tmp is RAM-backed here, and RAM is what runs out.
set(TEST_TMPDIR "$ENV{HOME}/storage/tmp" CACHE PATH "TMPDIR for the tests (scratch on disk, not tmpfs)")
file(MAKE_DIRECTORY "${TEST_TMPDIR}")
get_property(_all_tests DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _all_tests)
  set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT "TMPDIR=${TEST_TMPDIR}")
endforeach()

# tools/diskaudit/disk_audit.py (host-side audit of disk images: aliases, Finder
# icon positions, window frames and scroll, stray invisible files) on fixture
# images the test makes: an HFS volume through hfsutils, a small HFS+ one.
add_test(NAME disk_audit
  COMMAND ${Python3_EXECUTABLE} ${TEST_DIR}/test_disk_audit.py)
set_tests_properties(disk_audit PROPERTIES TIMEOUT 120 LABELS "tools" SKIP_RETURN_CODE 77)

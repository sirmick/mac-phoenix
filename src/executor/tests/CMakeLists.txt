cmake_minimum_required(VERSION 3.10)

# Configured on its own with Retro68's toolchain file, this builds the
# same tests as a 68k Mac application ("tests"), to run on a real System
# (tests/test_toolbox_suite.sh).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ExecutorToolboxTests C CXX)
endif()

function(add_compilefail_tests filename)
    configure_file(${filename} ${CMAKE_BINARY_DIR}/${filename}.junk)
    file(READ ${filename} text)
    string(REGEX MATCHALL "#ifdef COMPILEFAIL_[a-zA-Z0-9_]+" ifdefs ${text})
    get_filename_component(base ${filename} NAME)
    string(REGEX REPLACE "\\.[^.]*$" "" base ${base})

    foreach(ifdef ${ifdefs})
        string(REPLACE "#ifdef COMPILEFAIL_" "" testcase ${ifdef})

        set(target compilefail_${base}_${testcase})
        add_library(${target} EXCLUDE_FROM_ALL ${filename})
        target_compile_definitions(${target} PRIVATE -DCOMPILEFAIL -DCOMPILEFAIL_${testcase})

        target_link_libraries(${target} gtest romlib executor_platform_glue)
        target_compile_definitions(${target} PRIVATE -DEXECUTOR)
    
        add_test(NAME executor.compilefail.${base}.${testcase}
            COMMAND ${CMAKE_COMMAND} --build . --target ${target} --config $<CONFIGURATION>
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
        set_tests_properties(executor.compilefail.${base}.${testcase} PROPERTIES WILL_FAIL TRUE LABELS executor)
    endforeach()

    set(target compilefail_${base}_preamble)
    add_library(${target} EXCLUDE_FROM_ALL ${filename})
    target_compile_definitions(${target} PRIVATE -DCOMPILEFAIL)

    target_link_libraries(${target} gtest romlib executor_platform_glue)
    target_compile_definitions(${target} PRIVATE -DEXECUTOR)

    add_test(NAME executor.compilefail.${base}_preamble
        COMMAND ${CMAKE_COMMAND} --build . --target ${target} --config $<CONFIGURATION>
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

endfunction()

set(NATIVE_TEST_SOURCES
        files_internal.cpp guestvalues.cpp cpu.cpp
        sane.cpp
        localvolume_macbinary.cpp
        typeselect.cpp datetime.cpp numformat.cpp textedit.cpp sysheap.cpp
        sysroutines.cpp resprivate.cpp unknownselectors.cpp
        hfsplus_unicode.cpp hfsplus_volume.cpp
        # Native-only: drives the .IPP driver through the Device Manager.
        # The dual-mode ABI comparison against Apple's headers belongs in
        # TEST_SOURCES later, once the Retro68 side has MacTCP.h.
        mactcp.cpp
    )

set(TEST_SOURCES
        files.cpp quickdraw.cpp listmgr.cpp resources.cpp events.cpp
        # Dual-mode on purpose: natively this checks the generated
        # headers against defs/MacTCP.yaml, and under Retro68 -- built
        # against Apple's Universal Interfaces rather than generated
        # CIncludes -- it checks the documented ABI against Apple's.
        # Compiles to nothing where there is no MacTCP.h.
        mactcp_abi.cpp
    )
if(CMAKE_SYSTEM_NAME STREQUAL Retro68)
    list(APPEND TEST_SOURCES
            sane.cpp
        )
endif()


if(CMAKE_SYSTEM_NAME MATCHES Retro.*)
    #add_subdirectory(googletest)
    #target_link_libraries(tests gtest)

    set(GTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/googletest")
    include_directories("${GTEST_DIR}/include")
    include_directories("${GTEST_DIR}")
    add_definitions( -DGTEST_HAS_STREAM_REDIRECTION=0 -DGTEST_HAS_POSIX_RE=0 -DPATH_MAX=256)

    add_application(tests
        ${TEST_SOURCES} 
        main.cpp ${GTEST_DIR}/src/gtest-all.cc size.r)
    # One CODE segment. With Retro68's default segment map the tests land in
    # a ~1 MB segment that came out of the loader corrupted on both the real
    # System and Executor. Single-segment builds still crash in a way that
    # depends on code layout (WIP, see tests/test_toolbox_suite.sh).
    set_target_properties(tests PROPERTIES LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-single")

    # Second application from the same sources, built against RetroConsole so
    # that output goes to the text console instead of being redirected to "out".
    add_application(tests_console CONSOLE
        ${TEST_SOURCES}
        main.cpp ${GTEST_DIR}/src/gtest-all.cc size.r)
    target_compile_definitions(tests_console PRIVATE -DUSE_CONSOLE)
    set_target_properties(tests_console PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
else()
    include(GoogleTest)

    add_executable(executor_tests
        ${TEST_SOURCES}
        ${NATIVE_TEST_SOURCES}
        main_executor.cpp)

    target_link_libraries(executor_tests gtest romlib executor_platform_glue)

    # hfsplus_volume.cpp mounts the long-name fixture (made on Mac OS 9.0.4)
    set(HFSPLUS_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/longnames.img)
    add_custom_command(OUTPUT ${HFSPLUS_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hfsplus/longnames.img.gz
                -DOUT=${HFSPLUS_FIXTURE} -P ${CMAKE_CURRENT_SOURCE_DIR}/gunzip.cmake
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hfsplus/longnames.img.gz)
    add_custom_target(executor_hfsplus_fixtures DEPENDS ${HFSPLUS_FIXTURE})
    add_dependencies(executor_tests executor_hfsplus_fixtures)
    target_compile_definitions(executor_tests PRIVATE EXECUTOR_HFSPLUS_FIXTURE="${HFSPLUS_FIXTURE}")
    if(EMSCRIPTEN)
        # Under node: the host's files, as natively.
        target_link_options(executor_tests PRIVATE -sNODERAWFS=1)
    endif()
    target_compile_definitions(executor_tests PRIVATE -DEXECUTOR)
    target_compile_definitions(executor_tests PRIVATE EXECUTOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_options(executor_tests PRIVATE -Wno-multichar -fno-delete-null-pointer-checks)
    phoenix_exempt_from_hardening(executor_tests)

    gtest_discover_tests(executor_tests
        TEST_PREFIX "executor."
        PROPERTIES LABELS executor
        DISCOVERY_TIMEOUT 30)

    add_compilefail_tests(guestvalues.compfail.cpp)

    set_property(DIRECTORY
        APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test-properties.cmake"
    )
    target_compile_features(executor_tests PRIVATE cxx_std_17)

    # The HFS+ reader against hfsfuse's hfsdump, and the short names
    # against Mac OS 9.0.4's (docs/executor/HFSPlus.md); 77 without hfsdump.
    if(TARGET hfsplus_dump)
        find_package(Python3 COMPONENTS Interpreter)
        add_test(NAME executor.hfsplus_reader
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_hfsplus_reader.py
                    --dump $<TARGET_FILE:hfsplus_dump>)
        set_tests_properties(executor.hfsplus_reader PROPERTIES
            LABELS executor TIMEOUT 300 SKIP_RETURN_CODE 77)
        # The File Manager on HFS+ volumes: VolDump's dumps on Executor
        # against Mac OS 9.0.4's; 77 without tests-7.5.5.
        if(TARGET mac-phoenix)
            add_test(NAME executor.hfsplus_volume
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_hfsplus_volume.py
                        --dump $<TARGET_FILE:hfsplus_dump>)
            set_tests_properties(executor.hfsplus_volume PROPERTIES
                LABELS "executor;guest" TIMEOUT 300 SKIP_RETURN_CODE 77
                ENVIRONMENT "MACPHOENIX_EMULATOR=$<TARGET_FILE:mac-phoenix>")
            # Executor's System from an HFS+ image (hfs_sideload.cpp)
            add_test(NAME executor.hfsplus_sideload
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_hfsplus_sideload.py
                        --emulator $<TARGET_FILE:mac-phoenix> --dump $<TARGET_FILE:hfsplus_dump>)
            set_tests_properties(executor.hfsplus_sideload PROPERTIES
                LABELS executor TIMEOUT 120)
        endif()
    endif()
endif()

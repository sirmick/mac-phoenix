cmake_minimum_required(VERSION 3.10)

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

        target_link_libraries(${target} gtest romlib executor_uae_glue)
        target_compile_definitions(${target} PRIVATE -DEXECUTOR)
    
        add_test(NAME executor.compilefail.${base}.${testcase}
            COMMAND ${CMAKE_COMMAND} --build . --target ${target} --config $<CONFIGURATION>
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
        set_tests_properties(executor.compilefail.${base}.${testcase} PROPERTIES WILL_FAIL TRUE LABELS executor)
    endforeach()

    set(target compilefail_${base}_preamble)
    add_library(${target} EXCLUDE_FROM_ALL ${filename})
    target_compile_definitions(${target} PRIVATE -DCOMPILEFAIL)

    target_link_libraries(${target} gtest romlib executor_uae_glue)
    target_compile_definitions(${target} PRIVATE -DEXECUTOR)

    add_test(NAME executor.compilefail.${base}_preamble
        COMMAND ${CMAKE_COMMAND} --build . --target ${target} --config $<CONFIGURATION>
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

endfunction()

set(NATIVE_TEST_SOURCES
        files_internal.cpp guestvalues.cpp cpu.cpp
        sane.cpp
        localvolume_macbinary.cpp
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

    include_directories("googletest/googletest/include")
    include_directories("googletest/googletest/")
    add_definitions( -DGTEST_HAS_STREAM_REDIRECTION=0 -DGTEST_HAS_POSIX_RE=0 -DPATH_MAX=256)

    add_application(tests
        ${TEST_SOURCES} 
        main.cpp googletest/googletest/src/gtest-all.cc size.r)
    set_target_properties(tests PROPERTIES LINK_FLAGS "-Wl,-gc-sections")

    # Second application from the same sources, built against RetroConsole so
    # that output goes to the text console instead of being redirected to "out".
    add_application(tests_console CONSOLE
        ${TEST_SOURCES}
        main.cpp googletest/googletest/src/gtest-all.cc size.r)
    target_compile_definitions(tests_console PRIVATE -DUSE_CONSOLE)
    set_target_properties(tests_console PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
else()
    include(GoogleTest)

    add_executable(executor_tests
        ${TEST_SOURCES}
        ${NATIVE_TEST_SOURCES}
        main_executor.cpp)

    target_link_libraries(executor_tests gtest romlib executor_uae_glue)
    target_compile_definitions(executor_tests PRIVATE -DEXECUTOR)
    target_compile_definitions(executor_tests PRIVATE EXECUTOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_options(executor_tests PRIVATE -Wno-multichar -fno-delete-null-pointer-checks)
    executor_link_low(executor_tests)
    phoenix_exempt_from_hardening(executor_tests)

    gtest_discover_tests(executor_tests
        TEST_PREFIX "executor."
        PROPERTIES LABELS executor
        DISCOVERY_TIMEOUT 30)

    add_compilefail_tests(guestvalues.compfail.cpp)

    set_property(DIRECTORY
        APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test-properties.cmake"
    )
endif()

target_compile_features(executor_tests PRIVATE cxx_std_17)

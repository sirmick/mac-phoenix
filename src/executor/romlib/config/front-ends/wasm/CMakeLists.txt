# Executor in a browser tab (docs/executor/DIRECTION.md, W2): executor-web.js
# and .wasm, for web/executor's Worker. The wasm build only.
add_executable(executor-web web_main.cpp)
target_link_libraries(executor-web PRIVATE front-end-phoenix executor_platform_glue)
target_link_options(executor-web PRIVATE
    -sENVIRONMENT=worker
    -sEXIT_RUNTIME=0
    -sINITIAL_MEMORY=256MB
    -lworkerfs.js                         # uploaded disk images, not copied
    -sEXPORTED_RUNTIME_METHODS=FS,callMain)
# The page: web/executor next to the module, copied on every build (it
# changes without the module relinking).
add_custom_target(executor-web-page ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/web/executor $<TARGET_FILE_DIR:executor-web>
    COMMAND ${CMAKE_COMMAND} -E copy   # shared with mac-phoenix's client
            ${PROJECT_SOURCE_DIR}/client/cursor.js $<TARGET_FILE_DIR:executor-web>
    COMMENT "Executor's page")
add_dependencies(executor-web-page executor-web)

# The tab under headless Chromium (web/tests/tab_smoke.mjs). Skips without
# the playwright package: EXECUTOR_WEB_NODE_PATH is a node_modules holding
# it, EXECUTOR_WEB_CHROMIUM a browser other than the one it expects.
set(EXECUTOR_WEB_NODE_PATH "" CACHE PATH "node_modules with playwright, for executor.web.tab")
set(EXECUTOR_WEB_CHROMIUM "" CACHE FILEPATH "Chromium for executor.web.tab (default: playwright's)")
find_program(NODE_COMMAND node)
if(NODE_COMMAND)
    add_test(NAME executor.web.tab
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_smoke.mjs
                $<TARGET_FILE_DIR:executor-web> ${CMAKE_CURRENT_BINARY_DIR}/tab.png 60)
    set_tests_properties(executor.web.tab PROPERTIES
        LABELS "executor;web" TIMEOUT 120 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
    # W3: uploaded applications driven in the tab (PPCHello, the suite's
    # keyboard and audio sections).
    add_test(NAME executor.web.apps
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_apps.mjs
                $<TARGET_FILE_DIR:executor-web> ${PROJECT_SOURCE_DIR}/tests/guest)
    set_tests_properties(executor.web.apps PROPERTIES
        LABELS "executor;web" TIMEOUT 240 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# HFS+ in the tab: the long-name fixture mounted from the page (WORKERFS),
# VolDump's dump against Mac OS 9.0.4's (docs/executor/HFSPlus.md).
if(NODE_COMMAND)
    add_test(NAME executor.web.hfsplus
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_hfsplus.mjs
                $<TARGET_FILE_DIR:executor-web> ${PROJECT_SOURCE_DIR}/tests/guest
                ${PROJECT_SOURCE_DIR}/src/executor/tests/fixtures/hfsplus/longnames.img.gz)
    set_tests_properties(executor.web.hfsplus PROPERTIES
        LABELS "executor;web" TIMEOUT 180 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# HFS+ written in the tab (the overlay): VolWrite and VolDump against Mac OS
# 9.0.4's, fsck.hfsplus on the image downloaded from the page (docker).
if(NODE_COMMAND)
    add_test(NAME executor.web.hfsplus_write
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_hfsplus_write.mjs
                $<TARGET_FILE_DIR:executor-web> ${PROJECT_SOURCE_DIR}/tests/guest
                ${PROJECT_SOURCE_DIR}/src/executor/tests/fixtures/hfsplus/blank-long-64m.img.gz)
    set_tests_properties(executor.web.hfsplus_write PROPERTIES
        LABELS "executor;web" TIMEOUT 300 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# Executor's own volume kept in OPFS across reloads, and Reset.
if(NODE_COMMAND)
    add_test(NAME executor.web.persist
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_persist.mjs
                $<TARGET_FILE_DIR:executor-web>)
    set_tests_properties(executor.web.persist PROPERTIES
        LABELS "executor;web" TIMEOUT 120 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# W3: Apple's System and Finder from an uploaded image (EXECUTOR_WEB_SYSTEM:
# an HFS image with a blessed System Folder and a Finder, only read; the
# tests' tests-7.5.5.img by default). Skips without it.
set(EXECUTOR_WEB_SYSTEM "$ENV{HOME}/storage/images/tests-7.5.5.img" CACHE FILEPATH
    "System image for executor.web.finder")
if(NODE_COMMAND)
    add_test(NAME executor.web.finder
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_finder.mjs
                $<TARGET_FILE_DIR:executor-web> ${EXECUTOR_WEB_SYSTEM})
    # Writable images: a change kept, downloaded, reverted (overlay.js).
    add_test(NAME executor.web.write
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_write.mjs
                $<TARGET_FILE_DIR:executor-web> ${EXECUTOR_WEB_SYSTEM})
    set_tests_properties(executor.web.write PROPERTIES
        LABELS "executor;web" TIMEOUT 240 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
    set_tests_properties(executor.web.finder PROPERTIES
        LABELS "executor;web" TIMEOUT 240 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# W4: PowerPC in the tab on Apple's 8.6 System (an HFS+ image, the page's
# System choice and PowerPC box): the Finder, a disk's window, PPCHello;
# prints the times (web/tests/tab_ppc.mjs). Skips without the image. The
# browser profile (its OPFS holds the uploaded image) in the build tree,
# not a tmpfs /tmp.
set(EXECUTOR_WEB_SYSTEM_PPC "$ENV{HOME}/storage/images/8.6.img" CACHE FILEPATH
    "Mac OS 8.x System image for executor.web.ppc")
if(NODE_COMMAND)
    add_test(NAME executor.web.ppc
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_ppc.mjs
                $<TARGET_FILE_DIR:executor-web> ${PROJECT_SOURCE_DIR}/tests/guest ${EXECUTOR_WEB_SYSTEM_PPC})
    set_tests_properties(executor.web.ppc PROPERTIES
        LABELS "executor;web" TIMEOUT 300 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM};TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
endif()

# A PowerPC application in the tab: AppleWorks 5.0.3 on the same 8.6, up
# with its windows (it once jumped to 0 here: GetIndString past a list).
set(EXECUTOR_WEB_PPC_APPLEWORKS "$ENV{HOME}/storage/images/productivity-system-7.img" CACHE FILEPATH
    "Image with AppleWorks 5.0.3 for executor.web.ppc_appleworks")
if(NODE_COMMAND)
    add_test(NAME executor.web.ppc_appleworks
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_ppc.mjs
                $<TARGET_FILE_DIR:executor-web> ${PROJECT_SOURCE_DIR}/tests/guest ${EXECUTOR_WEB_SYSTEM_PPC}
                ${EXECUTOR_WEB_PPC_APPLEWORKS} "Productivity:AppleWorks 5.0.3 ƒ:AppleWorks")
    set_tests_properties(executor.web.ppc_appleworks PROPERTIES
        LABELS "executor;web" TIMEOUT 400 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM};TMPDIR=${CMAKE_CURRENT_BINARY_DIR};TAB_NO_HELLO=1")
endif()

# HiDPI (docs/executor/HiDPI.md): the same Finder booted at ?scale=1 and
# ?scale=2, checking the 2x run actually redraws text from outlines
# rather than the browser (or Skia) upscaling the 1x canvas -- a real
# check only with -DEXECUTOR_SKIA=ON (tools/skia/build-wasm.sh); without
# it, FAILs rather than skips, since that is exactly what the gate is for.
set(EXECUTOR_WEB_HIDPI_SYSTEM "$ENV{HOME}/storage/images/tests-7.6.1.img" CACHE FILEPATH
    "System image for executor.web.hidpi")
if(NODE_COMMAND)
    add_test(NAME executor.web.hidpi
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_hidpi.mjs
                $<TARGET_FILE_DIR:executor-web> ${EXECUTOR_WEB_HIDPI_SYSTEM})
    set_tests_properties(executor.web.hidpi PROPERTIES
        LABELS "executor;web;site" TIMEOUT 240 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# tools/web/server, the private service: its own checks (no browser), then
# the tab on it: signing in, invitations and sync between two browsers;
# a large library disk (EXECUTOR_WEB_SYSTEM and its record) read lazily.
find_program(PYTHON3_COMMAND python3)
if(PYTHON3_COMMAND)
    add_test(NAME executor.web.server
        COMMAND ${PYTHON3_COMMAND} ${PROJECT_SOURCE_DIR}/tools/web/server/test_server.py)
    set_tests_properties(executor.web.server PROPERTIES LABELS "executor;web" TIMEOUT 60)
endif()
if(NODE_COMMAND AND PYTHON3_COMMAND)
    add_test(NAME executor.web.server_sync
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_server_sync.mjs
                $<TARGET_FILE_DIR:executor-web>)
    add_test(NAME executor.web.server_lazy
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_server_lazy.mjs
                $<TARGET_FILE_DIR:executor-web> ${EXECUTOR_WEB_SYSTEM})
    set_tests_properties(executor.web.server_sync executor.web.server_lazy PROPERTIES
        LABELS "executor;web" TIMEOUT 300 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# The network relay: MacPhoenixSuite's network and mactcp sections and
# OTTestRetro68K in the tab, through tools/web/server's /relay to an echo
# server on this host (tests/lib/echo_server.py).
if(NODE_COMMAND AND PYTHON3_COMMAND)
    add_test(NAME executor.web.relay
        COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_relay.mjs
                $<TARGET_FILE_DIR:executor-web> ${PROJECT_SOURCE_DIR}/tests/guest)
    set_tests_properties(executor.web.relay PROPERTIES
        LABELS "executor;web" TIMEOUT 400 SKIP_RETURN_CODE 77
        ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM}")
endif()

# The assembled site (docs/executor/WebSite.md): every target application on
# its library disk (tools/web/make_library.py's, EXECUTOR_WEB_LIBRARY),
# opened from tools/web/server's library on each of the site's systems and
# used a moment; docs/executor/SCOREBOARD.md says which are expected to fail
# yet (web/tests/tab_site.mjs). Long (a reload and a launch an
# application), so label site, not executor: ctest -L site. Skip without
# the library.
set(EXECUTOR_WEB_LIBRARY "$ENV{HOME}/storage/web-library" CACHE PATH
    "tools/web/make_library.py's output, for the executor.web.site cells")
if(NODE_COMMAND AND PYTHON3_COMMAND)
    foreach(cell "7.6.1 productivity" "7.6.1 development" "7.6.1 games-68k"
                 "8.6 productivity" "8.6 development" "8.6 games-ppc")
        separate_arguments(cell)
        list(GET cell 0 system)
        list(GET cell 1 disk)
        add_test(NAME executor.web.site.${system}.${disk}
            COMMAND ${NODE_COMMAND} ${PROJECT_SOURCE_DIR}/web/tests/tab_site.mjs
                    $<TARGET_FILE_DIR:executor-web> ${EXECUTOR_WEB_LIBRARY} ${system} ${disk}.img)
        set_tests_properties(executor.web.site.${system}.${disk} PROPERTIES
            LABELS "web;site" TIMEOUT 1800 SKIP_RETURN_CODE 77
            ENVIRONMENT "NODE_PATH=${EXECUTOR_WEB_NODE_PATH};CHROMIUM=${EXECUTOR_WEB_CHROMIUM};TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
endif()

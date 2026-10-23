# Executor in a browser tab (docs/executor/DIRECTION.md, W2): executor-web.js
# and .wasm, for web/executor's Worker. The wasm build only.
add_executable(executor-web web_main.cpp)
target_link_libraries(executor-web PRIVATE front-end-phoenix executor_platform_glue)
target_link_options(executor-web PRIVATE
    -sENVIRONMENT=worker
    -sEXIT_RUNTIME=0
    -sINITIAL_MEMORY=256MB
    -lworkerfs.js                         # uploaded disk images, not copied
    -sEXPORTED_RUNTIME_METHODS=FS)
# The page: web/executor next to the module.
add_custom_command(TARGET executor-web POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/web/executor $<TARGET_FILE_DIR:executor-web>)

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

# executor-web's build id (web_main.cpp, the tab's log and status line):
# git describe of the tree it is built from. Written only when it changes,
# so web_main.cpp recompiles only then.
#   cmake -DSRC=DIR -DOUT=FILE -P build_id.cmake
execute_process(COMMAND git -C ${SRC} describe --always --dirty --abbrev=8
                OUTPUT_VARIABLE id OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT id)
    set(id "unknown")
endif()
set(text "#define EXECUTOR_WEB_BUILD \"${id}\"\n")
if(EXISTS ${OUT})
    file(READ ${OUT} old)
endif()
if(NOT old STREQUAL text)
    file(WRITE ${OUT} "${text}")
endif()

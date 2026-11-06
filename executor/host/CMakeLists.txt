# Front-end logic above the executor_host contract (session.h), shared by
# the tab (frontends/wasm) and native front ends: built natively too, so it
# stays a front end's to take.
add_library(executor-host STATIC
    session.h
    internal.h
    session.cpp
    images.cpp
    requests.cpp
    frames.cpp
)
target_include_directories(executor-host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(executor-host PUBLIC front-end-phoenix)
target_compile_options(executor-host PRIVATE ${EXECUTOR_COMMON_FLAGS})
phoenix_exempt_from_hardening(executor-host)

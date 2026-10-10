# MacPhoenix front end + host API. Linked into mac-phoenix.
add_library(front-end-phoenix
    phoenix.h
    phoenix.cpp
    executor_host.h
    executor_host.cpp
    phoenix_sound.h
    phoenix_sound.cpp
    ../../../main.cpp
)
target_include_directories(front-end-phoenix PUBLIC . ../headless)
target_compile_definitions(front-end-phoenix PRIVATE PHOENIX_EMBEDDED HEADLESS_ONLY PHOENIX_FRONT_END)
target_link_libraries(front-end-phoenix PUBLIC romlib front-end-headless Boost::program_options)

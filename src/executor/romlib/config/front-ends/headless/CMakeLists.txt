add_library(front-end-headless
    headless.h
    headless.cpp
)
target_include_directories(front-end-headless
PUBLIC .)
target_link_libraries(front-end-headless romlib)
# CPU backends (all compiled, runtime selection via --backend flag)

add_subdirectory(uae_cpu)
add_subdirectory(kpx)

add_library(cpu STATIC
  cpu_uae.c
  cpu_unicorn.cpp
  cpu_dualcpu.c
  cpu_trace.c
  uae_wrapper.cpp
  unicorn_wrapper.c
  unicorn_exec_loop.c
  unicorn_exception.c
  unicorn_validation.cpp
  emulop_c_wrapper.cpp
)

target_include_directories(cpu PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/platform
)

# Match uae_cpu JIT flags so wrappers can use #if USE_JIT
target_compile_definitions(cpu PRIVATE USE_JIT=1)

# Don't link uae_all/kpx here - let top-level CMakeLists handle link order
# with --start-group to resolve circular deps with core
target_link_libraries(cpu PRIVATE unicorn)

target_include_directories(cpu PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/uae_cpu
)

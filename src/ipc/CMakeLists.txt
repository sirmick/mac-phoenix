# IPC subsystem for subprocess mode (PPC)

add_library(ipc STATIC
  video_ipc_ppc.cpp
  control_ipc_child.cpp
  ipc_client.cpp
)

target_include_directories(ipc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

find_package(Qt6 6.4 COMPONENTS Core REQUIRED)

target_link_libraries(ipc PRIVATE
  Threads::Threads
  Qt6::Core
)

target_compile_options(ipc PRIVATE
  -Wno-sign-compare
  -Wno-class-memaccess
)

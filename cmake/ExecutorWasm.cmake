# =============================================================================
# The WebAssembly build: Executor alone (docs/executor/DIRECTION.md, W1).
#
#   emcmake cmake -B build-wasm
#   cmake --build build-wasm -j4
#   ctest --test-dir build-wasm -L executor     # the gtests under node
#
# One source tree, two builds: this is the same romlib, CPU facade and
# MAME cores as the native build, with the host edges' wasm sides
# (cpu/host/wasm, EXECUTOR_NET=none) and mame-ppc's C back-end only. None
# of mac-phoenix's parent (Qt, the web server, WebRTC), net-bridge, the
# ROM machines, UAE or asmjit.
# =============================================================================

set(BUILD_EXECUTOR ON)
set(EXECUTOR_NET none CACHE STRING "Executor's network backend: posix | none")

# No hardening flags here; the native build's helper is a no-op.
function(phoenix_exempt_from_hardening target)
endfunction()

# C++ exceptions (ExitToShell unwinds a process) and Asyncify (minicoro's
# Emscripten backend: processes and threads are fibers). JS-based
# exceptions: Asyncify does not take -fwasm-exceptions.
add_compile_options(-fexceptions)
add_link_options(
    -fexceptions
    -sASYNCIFY=1
    -sASYNCIFY_STACK_SIZE=262144
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=8MB          # the 68k emulator's host frames go deep
    -sEXIT_RUNTIME=1
    --profiling-funcs)        # function names in traces

enable_testing()
add_subdirectory(src/cpu/mame)
add_subdirectory(src/cpu/core)
add_subdirectory(src/executor)

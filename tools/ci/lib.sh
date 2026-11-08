# tools/ci/lib.sh: the tree, configuration and builds the CI scripts share
# (tools/ci/integration.sh, tools/ci/merge_gate.sh). Source it; each
# function returns non-zero on failure, its output in the log it is given.
#
# Configuration as the main tree's: Release, net-bridge, Executor
# regenerate, Skia, ccache; wasm with the EXECUTOR_WEB_* of
# /home/mick/src/mac-phoenix/build-wasm.

CI_WEBCACHE=/home/mick/src/mac-phoenix/build-wasm/CMakeCache.txt

# ci_tree REPO TREE REF LOG: TREE a worktree of REPO, detached at REF, clean
# but for its builds
ci_tree() {
    local repo=$1 tree=$2 ref=$3 log=$4
    if [[ ! -e "$tree/.git" ]]; then
        git -C "$repo" worktree add --detach "$tree" "$ref" >> "$log" 2>&1 || return 1
    fi
    # the builds rewrite committed binaries (BridgeAgent.bin, MacBrowser):
    # back to the ref's
    git -C "$tree" merge --abort >> "$log" 2>&1
    git -C "$tree" checkout --force --detach "$ref" >> "$log" 2>&1 || return 1
    git -C "$tree" clean -fdq -e build -e build-wasm -e toolchain -e ci-bin >> "$log" 2>&1
    mkdir -p "$tree/toolchain"
    [[ -e "$tree/toolchain/retro68" ]] || ln -s "$HOME/opt/retro68/build/toolchain" "$tree/toolchain/retro68"
}

# ci_configure_native TREE BUILD LOG
ci_configure_native() {
    cmake -B "$2" -S "$1" -DCMAKE_BUILD_TYPE=Release -DBUILD_NET_BRIDGE=ON \
        -DEXECUTOR_REGENERATE=ON -DEXECUTOR_SKIA=ON \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        > "$3" 2>&1
}

# ci_configure_wasm TREE BUILD LOG (emsdk sourced here)
ci_configure_wasm() {
    local node chromium
    node=$(grep -m1 "^EXECUTOR_WEB_NODE_PATH:" "$CI_WEBCACHE" 2>/dev/null | cut -d= -f2-)
    chromium=$(grep -m1 "^EXECUTOR_WEB_CHROMIUM:" "$CI_WEBCACHE" 2>/dev/null | cut -d= -f2-)
    # shellcheck disable=SC1091
    source "$HOME/emsdk/emsdk_env.sh" > /dev/null 2>&1
    emcmake cmake -B "$2" -S "$1" -DCMAKE_BUILD_TYPE=Release -DEXECUTOR_SKIA=ON \
        -DEXECUTOR_WEB_NODE_PATH="$node" -DEXECUTOR_WEB_CHROMIUM="$chromium" \
        > "$3" 2>&1
}

# ci_build NAME TREE BUILD LOG JOBS: cmake --build (native: macbrowser-app
# -j1 first), the committed binaries put back after
ci_build() {
    local name=$1 tree=$2 dir=$3 log=$4 jobs=$5 rc
    { [[ "$name" == wasm ]] || cmake --build "$dir" --target macbrowser-app -j1; } > "$log" 2>&1 &&
        cmake --build "$dir" -j"$jobs" >> "$log" 2>&1
    rc=$?
    git -C "$tree" checkout -- BridgeAgent MacBrowser 2>/dev/null
    return $rc
}

# ci_first_error LOG: the build's first error line, for a table
ci_first_error() {
    grep -m1 -E 'error:|Error [0-9]|CMake Error' "$1" | cut -c1-120 | tr '|' '/'
}

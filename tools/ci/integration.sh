#!/usr/bin/env bash
# tools/ci/integration.sh [options]: a fresh integration run of a branch
# (default executor-86-grid2): its own git worktree (<ci>/integration, detached at
# the branch's head), the native and wasm builds there, then
#   ctest -L executor            native, the emulator run from a copy
#                                (<ci>/integration/ci-bin), difftests made to run
#                                their probes (DIFFTEST_NO_SKIP=1, own out dir)
#   ctest -L executor            wasm (the gtests under node, executor.web.*)
#   ctest -L site                wasm (the assembled site, long)
# and a dated summary: <ci>/runs/<YYYY-MM-DD-HHMM>/summary.md (+ logs, JUnit
# XML), <ci>/latest pointing at it. <ci> is ~/storage/ci. Its tests claim
# free ports as they start (tests/lib/ports.py), so they run beside anyone
# else's. One run at a time (<ci>/.lock).
#
#   --branch REF     what to test (default executor-86-grid2)
#   --jobs N         ctest -j (default 2); builds are always -j2
#   --only LIST      some of: native wasm site (default all three)
#   --no-build       test what is built (no checkout either)
#   --ctest-args A   more ctest arguments (e.g. "-R difftest_")
#
# Exit status: 0 if every suite passed, 1 otherwise. Configuration as the
# main tree's (Release, net-bridge, Executor regenerate, Skia; wasm with the
# EXECUTOR_WEB_* of /home/mick/src/mac-phoenix/build-wasm).
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
CI="${MACPHOENIX_CI_DIR:-$HOME/storage/ci}"
BRANCH=executor-86-grid2
JOBS=2
ONLY="native wasm site"
BUILD=1
CTEST_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --branch) BRANCH="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --only) ONLY="$2"; shift 2 ;;
        --no-build) BUILD=0; shift ;;
        --ctest-args) read -ra CTEST_ARGS <<< "$2"; shift 2 ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done

TREE="$CI/integration"
STAMP=$(date +%Y-%m-%d-%H%M)
RUN="$CI/runs/$STAMP"
mkdir -p "$RUN"
exec 9>"$CI/.lock"
if ! flock -n 9; then
    echo "another integration run holds $CI/.lock; waiting" >&2
    flock 9
fi
ln -sfn "runs/$STAMP" "$CI/latest"
SUMMARY="$RUN/summary.md"
touch "$RUN/.start"     # reports newer than this are the run's
STATUS=0
T0=$(date +%s)
log() { echo "[$(date +%H:%M:%S)] $*"; }
has() { [[ " $ONLY " == *" $1 "* ]]; }

# --- the tree -------------------------------------------------------------
if (( BUILD )); then
    if [[ ! -e "$TREE/.git" ]]; then
        git -C "$REPO" worktree add --detach "$TREE" "$BRANCH" > "$RUN/checkout.log" 2>&1 || {
            echo "worktree add failed: $RUN/checkout.log" >&2; exit 1; }
    fi
    # the builds rewrite committed binaries (BridgeAgent.bin, MacBrowser):
    # back to the branch's
    git -C "$TREE" checkout --force --detach "$BRANCH" >> "$RUN/checkout.log" 2>&1 || {
        echo "checkout failed: $RUN/checkout.log" >&2; exit 1; }
    git -C "$TREE" clean -fdq -e build -e build-wasm -e toolchain -e ci-bin >> "$RUN/checkout.log" 2>&1
fi
COMMIT=$(git -C "$TREE" rev-parse --short HEAD)
SUBJECT=$(git -C "$TREE" log -1 --format=%s)
mkdir -p "$TREE/toolchain"
[[ -e "$TREE/toolchain/retro68" ]] || ln -s "$HOME/opt/retro68/build/toolchain" "$TREE/toolchain/retro68"

{
    echo "# Integration run $STAMP"
    echo
    echo "\`$BRANCH\` at \`$COMMIT\` ($SUBJECT), tree $TREE,"
    echo "port base $(python3 "$TREE/tests/lib/ports.py" base 2>/dev/null || echo '?')."
    echo
    echo "| step | result | time | log |"
    echo "|---|---|---|---|"
} > "$SUMMARY"
row() { echo "| $1 | $2 | $3 | $4 |" >> "$SUMMARY"; }
mins() { printf '%dm%02ds' $(( $1 / 60 )) $(( $1 % 60 )); }

build() {   # name dir log: cmake --build, -j2 (macbrowser-app -j1 first)
    local t=$(date +%s) rc
    log "build $1"
    { [[ "$1" == wasm ]] || cmake --build "$2" --target macbrowser-app -j1; } > "$3" 2>&1 &&
        cmake --build "$2" -j2 >> "$3" 2>&1
    rc=$?
    git -C "$TREE" checkout -- BridgeAgent MacBrowser 2>/dev/null
    if (( rc )); then
        row "build $1" "**failed**: $(grep -m1 -E 'error:|Error [0-9]' "$3" | cut -c1-120 | tr '|' '/')" "$(mins $(( $(date +%s) - t )))" "$3"
        STATUS=1
    else
        row "build $1" ok "$(mins $(( $(date +%s) - t )))" "$3"
    fi
    return $rc
}

suite() {   # name builddir label env...: ctest, a row, failures listed
    local name=$1 dir=$2 label=$3 t=$(date +%s) out="$RUN/ctest-$1.log" rc
    shift 3
    log "ctest $name"
    env "$@" ctest --test-dir "$dir" -L "$label" -j"$JOBS" --output-on-failure \
        --output-junit "$RUN/junit-$name.xml" "${CTEST_ARGS[@]}" > "$out" 2>&1
    rc=$?
    local line; line=$(grep -E 'tests passed, [0-9]+ tests failed' "$out" | tail -1)
    local skipped; skipped=$(grep -cE '\*\*\*Skipped|Not Run' "$out")
    row "ctest -L $label ($name)" "$( (( rc )) && echo "**$line**" || echo "${line:-no tests}")${skipped:+, $skipped skipped}" \
        "$(mins $(( $(date +%s) - t )))" "$out"
    (( rc )) && STATUS=1
    {
        echo
        echo "### $name: failed"
        echo
        sed -n '/The following tests FAILED:/,$p' "$out" | grep -E '^\s+[0-9]+ - ' | sed 's/^\s*/- /'
        # the checks that failed, as the tests print them
        grep -E '^ *FAIL[: ]' "$out" | sort -u | head -40 | sed 's/^ */  - /'
        echo
        echo "### $name: skipped"
        echo
        grep -E '\*\*\*Skipped|Not Run' "$out" | sed -E 's/^ *[0-9]+\/[0-9]+ Test +#[0-9]+: //; s/ +\.+/ /; s/^/- /'
    } >> "$RUN/details.md"
}

if has native; then
    NB="$TREE/build"
    ok=1
    if (( BUILD )); then
        cmake -B "$NB" -S "$TREE" -DCMAKE_BUILD_TYPE=Release -DBUILD_NET_BRIDGE=ON \
            -DEXECUTOR_REGENERATE=ON -DEXECUTOR_SKIA=ON \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
            > "$RUN/configure-native.log" 2>&1 && build native "$NB" "$RUN/build-native.log" || ok=0
    fi
    if (( ok )); then
        # the tests run a copy (a rebuild mid-run can't pull it from under
        # them); BridgeAgent/ beside the copy's parent, as in the tree
        mkdir -p "$TREE/ci-bin"
        cp -f "$NB/mac-phoenix" "$TREE/ci-bin/mac-phoenix"
        suite native "$NB" executor \
            MACEMU_BINARY="$TREE/ci-bin/mac-phoenix" MACPHOENIX_EMULATOR="$TREE/ci-bin/mac-phoenix" \
            DIFFTEST_NO_SKIP=1
        # the difftests' own verdicts (first line of each report)
        {
            echo
            echo "### difftests"
            echo
            for r in "$HOME/storage/difftest/wt-integration"/*/report-executor-8.6.md; do
                [[ -e "$r" && "$r" -nt "$RUN/.start" ]] || continue
                echo "- $(basename "$(dirname "$r")"): $(sed -n 3p "$r") ($r)"
            done
        } >> "$RUN/details.md"
    fi
fi

if has wasm || has site; then
    WB="$TREE/build-wasm"
    ok=1
    if (( BUILD )); then
        WEBCACHE=/home/mick/src/mac-phoenix/build-wasm/CMakeCache.txt
        webopt() { grep -m1 "^$1:" "$WEBCACHE" 2>/dev/null | cut -d= -f2-; }
        # shellcheck disable=SC1091
        source "$HOME/emsdk/emsdk_env.sh" > /dev/null 2>&1
        emcmake cmake -B "$WB" -S "$TREE" -DCMAKE_BUILD_TYPE=Release -DEXECUTOR_SKIA=ON \
            -DEXECUTOR_WEB_NODE_PATH="$(webopt EXECUTOR_WEB_NODE_PATH)" \
            -DEXECUTOR_WEB_CHROMIUM="$(webopt EXECUTOR_WEB_CHROMIUM)" \
            > "$RUN/configure-wasm.log" 2>&1 && build wasm "$WB" "$RUN/build-wasm.log" || ok=0
    fi
    if (( ok )); then
        has wasm && suite wasm "$WB" executor
        has site && suite site "$WB" site
    fi
fi

{
    echo
    echo "Total $(mins $(( $(date +%s) - T0 ))): $( (( STATUS )) && echo FAILED || echo passed)."
    [[ -e "$RUN/details.md" ]] && cat "$RUN/details.md"
} >> "$SUMMARY"
rm -f "$RUN/details.md" "$RUN/.start"
log "summary: $SUMMARY"
exit $STATUS

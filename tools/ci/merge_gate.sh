#!/usr/bin/env bash
# tools/ci/merge_gate.sh BRANCH [options]: would merging BRANCH break the
# target (default executor-86-grid2)? In its own git worktree (<ci>/gate,
# detached at the target's head; the main checkout and its index are never
# touched):
#   git merge --no-ff BRANCH     a conflict stops here: FAIL, the files listed
#   native and wasm builds       (tools/ci/lib.sh) a failed one: FAIL, no tests
#   ctest -L 'executor|qt'       native, the emulator run from a copy
#   ctest -L executor            wasm
# each suite's failures rerun alone (tools/ci/rerun_flaky.py, tests/flaky.txt),
# then a one-screen verdict in <ci>/gate/<branch>-<date>.md (logs beside it,
# in <branch>-<date>/), <ci> being ~/storage/ci:
#   MERGE        everything passed
#   FLAKES-ONLY  every failure passed alone (new flakes named: add them to
#                tests/flaky.txt?)
#   FAIL         a conflict, a failed build, or a test that fails alone
# The tested merge commit is kept as refs/ci-gate/<branch>.
#
#   --into REF       the target branch (default executor-86-grid2)
#   --jobs N         ctest -j (default 4)
#   --build-jobs N   cmake --build -j (default 4)
#   --apply          with MERGE: move the target to the tested merge commit
#                    (git update-ref, the old value checked: a change to the
#                    target meanwhile aborts). A target checked out in a
#                    worktree (the main checkout) is not moved under it: the
#                    command that fast-forwards it is printed instead
#   -m MSG           the merge commit's message (default "Merge BRANCH")
#   --ctest-args A   more ctest arguments, both suites (e.g. "-R qt_")
#
# Exit status: 0 MERGE, 3 FLAKES-ONLY, 1 FAIL, 2 a usage error.
# One gate at a time (<ci>/gate/.lock).
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=lib.sh
source "$REPO/tools/ci/lib.sh"
CI="${MACPHOENIX_CI_DIR:-$HOME/storage/ci}"
INTO=executor-86-grid2
JOBS=4
BUILD_JOBS=4
APPLY=0
MSG=""
CTEST_ARGS=()
BRANCH=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --into) INTO="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --build-jobs) BUILD_JOBS="$2"; shift 2 ;;
        --apply) APPLY=1; shift ;;
        -m) MSG="$2"; shift 2 ;;
        --ctest-args) read -ra CTEST_ARGS <<< "$2"; shift 2 ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) [[ -z "$BRANCH" ]] || { echo "one BRANCH only" >&2; exit 2; }; BRANCH="$1"; shift ;;
    esac
done
[[ -n "$BRANCH" ]] || { sed -n '2,32p' "$0"; exit 2; }
TIP=$(git -C "$REPO" rev-parse --verify -q "$BRANCH^{commit}") || { echo "no branch $BRANCH" >&2; exit 2; }
git -C "$REPO" rev-parse --verify -q "refs/heads/$INTO" > /dev/null || { echo "no branch $INTO" >&2; exit 2; }
MSG=${MSG:-"Merge $BRANCH"}

GATE="$CI/gate"
TREE="$GATE/tree"
mkdir -p "$GATE"
exec 9>"$GATE/.lock"
if ! flock -n 9; then
    echo "another gate holds $GATE/.lock; waiting" >&2
    flock 9
fi
NAME=$(tr '/' '-' <<< "$BRANCH")
STAMP=$(date +%Y-%m-%d-%H%M)
REPORT="$GATE/$NAME-$STAMP.md"
LOGS="$GATE/$NAME-$STAMP"
mkdir -p "$LOGS"
# the target as it is now: --apply moves it only from this
OLD=$(git -C "$REPO" rev-parse "refs/heads/$INTO")
T0=$(date +%s)
log() { echo "[$(date +%H:%M:%S)] $*"; }
mins() { printf '%dm%02ds' $(( $1 / 60 )) $(( $1 % 60 )); }
STEPS=()        # "| step | result | time | log |" rows
step() { STEPS+=("| $1 | $2 | $3 | $4 |"); }
MERGE=""
VERDICT=""
DETAIL=()       # lines under the table

verdict() {     # VERDICT [exit]: write the report, print it, exit
    VERDICT=$1
    {
        echo "# $VERDICT: $BRANCH into $INTO"
        echo
        echo "\`$BRANCH\` at \`$(git -C "$REPO" rev-parse --short "$TIP")\` into \`$INTO\` at" \
             "\`$(git -C "$REPO" rev-parse --short "$OLD")\`${MERGE:+, merge \`$(git -C "$REPO" rev-parse --short "$MERGE")\`}," \
             "$(date '+%Y-%m-%d %H:%M'), $(mins $(( $(date +%s) - T0 ))) (tree $TREE)."
        echo
        echo "| step | result | time | log |"
        echo "|---|---|---|---|"
        printf '%s\n' "${STEPS[@]}"
        (( ${#DETAIL[@]} )) && { echo; printf '%s\n' "${DETAIL[@]}"; }
    } > "$REPORT"
    echo
    cat "$REPORT"
    echo
    echo "report: $REPORT"
}

apply() {
    local where
    where=$(git -C "$REPO" worktree list --porcelain |
        awk -v b="branch refs/heads/$INTO" '/^worktree /{w=substr($0,10)} $0==b{print w}')
    if [[ -n "$where" ]]; then
        echo "$INTO is checked out in $where: not moved under it. To take the merge:"
        echo "    git -C $where merge --ff-only $MERGE"
        echo "(refuses if $INTO has moved since $(git -C "$REPO" rev-parse --short "$OLD"))"
    elif git -C "$REPO" update-ref -m "merge_gate: $MSG" "refs/heads/$INTO" "$MERGE" "$OLD"; then
        echo "$INTO moved $(git -C "$REPO" rev-parse --short "$OLD") -> $(git -C "$REPO" rev-parse --short "$MERGE")."
        echo "A checkout of $INTO takes it with: git merge --ff-only $INTO"
    else
        echo "$INTO changed during the gate (no longer $OLD): not moved; run the gate again" >&2
        return 1
    fi
}

# --- the merge ---------------------------------------------------------------
t=$(date +%s)
log "tree $TREE at $INTO ($(git -C "$REPO" rev-parse --short "$OLD"))"
if ! ci_tree "$REPO" "$TREE" "$OLD" "$LOGS/checkout.log"; then
    step checkout "**failed**" "$(mins $(( $(date +%s) - t )))" "$LOGS/checkout.log"
    verdict FAIL; exit 1
fi
log "merge $BRANCH"
if ! git -C "$TREE" merge --no-ff -m "$MSG" "$TIP" > "$LOGS/merge.log" 2>&1; then
    mapfile -t CONFLICTS < <(git -C "$TREE" diff --name-only --diff-filter=U)
    git -C "$TREE" merge --abort >> "$LOGS/merge.log" 2>&1
    step "merge" "**conflict**: ${#CONFLICTS[@]} files" "$(mins $(( $(date +%s) - t )))" "$LOGS/merge.log"
    DETAIL+=("Conflicts:" "" "${CONFLICTS[@]/#/- }")
    (( ${#CONFLICTS[@]} )) || DETAIL+=("- (none listed: $(tail -1 "$LOGS/merge.log"))")
    verdict FAIL; exit 1
fi
MERGE=$(git -C "$TREE" rev-parse HEAD)
git -C "$REPO" update-ref "refs/ci-gate/$NAME" "$MERGE"
step merge "ok: $(git -C "$TREE" diff --shortstat "$OLD" HEAD | sed 's/^ *//')" "$(mins $(( $(date +%s) - t )))" "$LOGS/merge.log"

# --- the builds --------------------------------------------------------------
NB="$TREE/build"
WB="$TREE/build-wasm"
gate_build() {   # name configure-function dir
    local t=$(date +%s)
    log "build $1"
    if ! "$2" "$TREE" "$3" "$LOGS/configure-$1.log"; then
        step "configure $1" "**failed**: $(ci_first_error "$LOGS/configure-$1.log")" \
            "$(mins $(( $(date +%s) - t )))" "$LOGS/configure-$1.log"
        return 1
    fi
    if ! ci_build "$1" "$TREE" "$3" "$LOGS/build-$1.log" "$BUILD_JOBS"; then
        step "build $1" "**failed**: $(ci_first_error "$LOGS/build-$1.log")" \
            "$(mins $(( $(date +%s) - t )))" "$LOGS/build-$1.log"
        return 1
    fi
    step "build $1" ok "$(mins $(( $(date +%s) - t )))" "$LOGS/build-$1.log"
}
# a failed build: no tests (they would run the last good binary)
gate_build native ci_configure_native "$NB" || { verdict FAIL; exit 1; }
gate_build wasm ci_configure_wasm "$WB" || { verdict FAIL; exit 1; }

# --- the tests ---------------------------------------------------------------
# the tests run a copy (a rebuild mid-run can't pull it from under them);
# BridgeAgent/ beside the copy's parent, as in the tree
mkdir -p "$TREE/ci-bin"
cp -f "$NB/mac-phoenix" "$TREE/ci-bin/mac-phoenix"
NATIVE_ENV=(MACEMU_BINARY="$TREE/ci-bin/mac-phoenix" MACPHOENIX_EMULATOR="$TREE/ci-bin/mac-phoenix"
            DIFFTEST_NO_SKIP=1)
WORST=0         # 0 passed, 3 flakes only, 1 a real failure
NEW_FLAKES=()
suite() {   # name builddir label env...
    local name=$1 dir=$2 label=$3 t=$(date +%s) out="$LOGS/ctest-$1.log" rc line envargs=()
    local col="ctest -L '${3//|/\\|}' ($1)"    # (| escaped in the table)
    shift 3
    log "ctest $name -L '$label'"
    rm -f "$dir/Testing/Temporary/LastTestsFailed.log"
    env "$@" ctest --test-dir "$dir" -L "$label" -j"$JOBS" --output-on-failure \
        --output-junit "$LOGS/junit-$name.xml" "${CTEST_ARGS[@]}" > "$out" 2>&1
    rc=$?
    line=$(grep -E 'tests passed, [0-9]+ tests failed' "$out" | tail -1)
    if (( rc == 0 )); then
        step "$col" "${line:-no tests}" "$(mins $(( $(date +%s) - t )))" "$out"
        return
    fi
    if [[ -z "$line" ]]; then   # ctest itself failed (no tests, bad arguments)
        step "$col" "**ctest failed** (exit $rc)" "$(mins $(( $(date +%s) - t )))" "$out"
        WORST=1
        return
    fi
    for e in "$@"; do envargs+=(--env "$e"); done
    log "rerun $name's failures alone"
    # the gate's own rerun script; the merged tree's list (BRANCH may add to it)
    local flaky="$TREE/tests/flaky.txt"
    [[ -e "$flaky" ]] || flaky="$REPO/tests/flaky.txt"
    python3 "$REPO/tools/ci/rerun_flaky.py" --test-dir "$dir" --log "$out" --out "$LOGS/rerun-$name" \
        --flaky "$flaky" "${envargs[@]}" > /dev/null 2> "$LOGS/rerun-$name.log"
    rc=$?
    local json="$LOGS/rerun-$name/rerun.json" counts
    counts=$(python3 -c '
import json, sys, collections
d = json.load(open(sys.argv[1]))
c = collections.Counter(t["class"] for t in d["tests"])
print(", ".join(f"{n} {k}" for k, n in sorted(c.items())))
for t in d["tests"]:
    print("\x1f".join([t["class"], t["name"], t["alone"], t["reason"], t["log"]]))
' "$json" 2>/dev/null)
    step "$col" "**$line**; alone: ${counts%%$'\n'*}" "$(mins $(( $(date +%s) - t )))" "$out"
    DETAIL+=("$name, each failure rerun alone ($LOGS/rerun-$name/rerun.md):")
    local cls tname alone reason tlog
    # fields split on US (\x1f), not tab: IFS whitespace would fold empty ones
    while IFS=$'\x1f' read -r cls tname alone reason tlog; do
        case "$cls" in
            real) DETAIL+=("- **$tname**: fails alone ($alone): $tlog") ;;
            flake) DETAIL+=("- $tname: flake, passes alone (flaky.txt: $reason)") ;;
            new-flake) DETAIL+=("- **$tname**: passes alone, not in tests/flaky.txt: new flake, add it? ($tlog)")
                       NEW_FLAKES+=("$tname") ;;
        esac
    done < <(tail -n +2 <<< "$counts")
    case $rc in
        3|4) (( WORST == 1 )) || WORST=3 ;;
        *) WORST=1 ;;
    esac
}
suite native "$NB" 'executor|qt' "${NATIVE_ENV[@]}"
suite wasm "$WB" executor

case $WORST in
    0) verdict MERGE ;;
    3) verdict FLAKES-ONLY ;;
    *) verdict FAIL ;;
esac
if (( APPLY )); then
    if [[ "$VERDICT" == MERGE ]]; then
        apply || exit 1
    else
        echo "--apply: $VERDICT, $INTO not moved (tested merge: refs/ci-gate/$NAME, $MERGE)"
    fi
else
    echo "tested merge: refs/ci-gate/$NAME ($MERGE); --apply to move $INTO"
fi
case $VERDICT in MERGE) exit 0 ;; FLAKES-ONLY) exit 3 ;; *) exit 1 ;; esac

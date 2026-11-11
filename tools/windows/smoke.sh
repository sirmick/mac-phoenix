#!/bin/bash
# The Windows zip under Wine (a Debian container, tools/windows/wine.Dockerfile):
#   - executor-desktop.exe offscreen to Executor's own System's shell: a
#     screenshot, the log file in the data folder, its first line the version;
#   - with --system IMAGE (and --ppc): that System's Finder;
#   - --crash-test writes crash-*.dmp and crash-*.txt beside the log;
#   - executor-standalone.exe serves the tab (curl).
#
#   tools/windows/smoke.sh ZIP WORKDIR [--system IMAGE [--ppc] [--scale N]]
#
# IMAGE is a path on this machine (mounted read-only); the run's data stays
# in WORKDIR. --scale N: the System's run draws at N (HiDPI, a Skia build):
# its screenshot is then also taken at N (apple-hidpi.png). Exit 0: every
# check passed.
set -uo pipefail
ZIP=$(realpath "$1")
WORK=$(realpath -m "$2")
shift 2
SYSTEM= PPC= SCALE=
while [ $# -gt 0 ]; do
    case $1 in
        --system) SYSTEM=$(realpath "$2"); shift ;;
        --ppc) PPC=--ppc ;;
        --scale) SCALE=$2; shift ;;
    esac
    shift
done
IMAGE=macphoenix-wine:13
if ! docker image inspect $IMAGE > /dev/null 2>&1; then
    docker build -t $IMAGE -f "$(dirname "$0")/wine.Dockerfile" "$(dirname "$0")" || exit 1
fi

rm -rf "$WORK"
mkdir -p "$WORK/.home"
python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$ZIP" "$WORK/app"
APP=$(ls -d "$WORK"/app/executor-windows-*)
APPNAME=$(basename "$APP")
VOLS=(-v "$WORK:/work")
[ -n "$SYSTEM" ] && VOLS+=(-v "$(dirname "$SYSTEM"):/sys:ro")

# wine in the container, as this user; LANG: Wine's Unix file names in UTF-8
wine_run() {
    timeout 300 docker run --rm --user "$(id -u):$(id -g)" -e HOME=/work/.home -e LANG=C.UTF-8 \
        -e WINEDEBUG=-all -e QT_QPA_PLATFORM=offscreen "${VOLS[@]}" $IMAGE bash -c "$1"
}
EXE="wine /work/app/$APPNAME/executor-desktop.exe"
fails=0
check() {  # check WHAT COMMAND...
    local what=$1; shift
    if "$@"; then echo "ok   $what"; else echo "FAIL $what"; fails=$((fails + 1)); fi
}

echo "== Executor's own System"
wine_run "$EXE --data 'Z:\\work\\own' --screenshot 'Z:\\work\\own.png' --screenshot-delay 3 --quit --timeout 120 > /work/own.txt 2>&1"
check "own System: in its event loop" grep -q 'is in its event loop' "$WORK/own.txt"
check "own System: screenshot" test -s "$WORK/own.png"
log=$(ls "$WORK"/own/logs/executor-*.log 2>/dev/null | head -1)
check "log file in the data folder (${log#$WORK/})" test -s "$log"
check "the log's first line is the version" bash -c "head -1 '$log' | grep -q '^\[desktop\] Executor [^ ]* on Windows'"
check "UTF-8 code page" grep -q 'code page 65001' "$WORK/own.txt"
check "no crash report files from a run that didn't crash" bash -c "! ls '$WORK'/own/logs/crash-* > /dev/null 2>&1"

if [ -n "$SYSTEM" ]; then
    echo "== $(basename "$SYSTEM") $PPC ${SCALE:+--scale $SCALE}"
    wine_run "$EXE --data 'Z:\\work\\apple' --system 'Z:\\sys\\$(basename "$SYSTEM")' $PPC ${SCALE:+--scale $SCALE} --screenshot 'Z:\\work\\apple.png' --screenshot-delay 3 --quit --timeout 150 > /work/apple.txt 2>&1"
    check "Apple's System: the Finder in its event loop" grep -q 'Finder is in its event loop' "$WORK/apple.txt"
    check "Apple's System: screenshot" test -s "$WORK/apple.png"
    if [ -n "$SCALE" ]; then
        check "--scale $SCALE: Skia, not the 1x fallback" bash -c "! grep -q 'built without Skia' '$WORK/apple.txt'"
        check "--scale $SCALE: the screenshot at that scale" test -s "$WORK/apple-hidpi.png"
    fi
fi

echo "== --crash-test"
wine_run "$EXE --data 'Z:\\work\\crash' --crash-test --timeout 60 > /work/crash.txt 2>&1"
check "crash report .txt" bash -c "ls '$WORK'/crash/logs/crash-*.txt > /dev/null 2>&1"
check "crash report .dmp" bash -c "ls '$WORK'/crash/logs/crash-*.dmp > /dev/null 2>&1"
check "the report names the access violation" bash -c "grep -q 'access violation' '$WORK'/crash/logs/crash-*.txt"

echo "== --crash-test-heap (the process heap locked, as a crash inside malloc leaves it)"
wine_run "$EXE --data 'Z:\\work\\crashheap' --crash-test-heap --timeout 120 > /work/crashheap.txt 2>&1"
check "heap locked: the text report still comes" bash -c "grep -q 'access violation' '$WORK'/crashheap/logs/crash-*.txt"
check "heap locked: the CRASH line in the log" bash -c "grep -q 'CRASH: access violation' '$WORK'/crashheap/logs/executor-*.log"

echo "== executor-standalone.exe"
# one cookie jar: the first browser claims the server (its cookie), others get 403
wine_run "wine /work/app/$APPNAME/executor-standalone.exe --data 'Z:\\work\\web' --port 18095 > /work/standalone.txt 2>&1 &
          for i in \$(seq 60); do curl -sf -c /tmp/jar -b /tmp/jar -o /work/index.html http://127.0.0.1:18095/ && break; sleep 1; done
          curl -s -c /tmp/jar -b /tmp/jar -o /dev/null -w '%{http_code} %{size_download}\n' http://127.0.0.1:18095/executor-web.wasm > /work/wasm.txt"
check "the page is served" grep -qi '<html' "$WORK/index.html"
check "the wasm is served" grep -q '^200 ' "$WORK/wasm.txt"

echo "$fails failed"
[ $fails -eq 0 ]

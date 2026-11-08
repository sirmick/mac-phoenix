#!/bin/bash
# test_qt_apps_executor.sh [BINARY] [PORT]: QuickTime movies in real
# applications on Executor 7.6.1, through Executor's movie controller
# (executor/core/romlib/mc.cpp; docs/executor/QuickTime.md). 7.6.1's
# "Demo click&drag" (tests/guest/qt/disk_movies.sh) is opened in MoviePlayer
# 2.5 and in SimpleText 1.4, and HyperCard 2.4.1's Simple Movie in a windoid
# by the QuickTime Tools stack's Movie XCMD (a HyperTalk "movie" command
# over /api/script; dev-tools-system-7.img, a copy mounted writable, or that
# part is skipped); in each:
#   - the movie comes up drawn, with the controller's bar under it;
#   - a click on the bar's play button plays it: the movie's pixels change,
#     and its sound is heard (--audio-dump);
#   - in MoviePlayer, a click on the bar's speaker mutes it: played again,
#     nothing is heard; a second click unmutes it;
#   - the application quits.
# Skipped without the movie (its image) or the test disk.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="${1:-${MACEMU_BINARY:-$PROJECT_ROOT/build/mac-phoenix}}"
PORT="${2:-18880}"
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # a free port (docs/Testing.md "Ports")
[[ -x "$BINARY" ]] || { echo "SKIP: no binary $BINARY"; exit 77; }
CACHE="${QT_STORE:-$HOME/storage/qt}/disk-movies"
MOVIES=$(bash "$SCRIPT_DIR/guest/qt/disk_movies.sh" "$CACHE")
[[ $'\n'"$MOVIES"$'\n' == *$'\n'demo-click$'\n'* ]] || { echo "SKIP: no Demo click&drag (7.6.1.img)"; exit 77; }
DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.6.1 $$)}"
[[ -f "$DISK" ]] || { echo "SKIP: no test disk"; exit 77; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/mactest-qtapps.XXXXXX")
EXTFS_DIR="$WORK/host"
LOG="$WORK/emulator.log"
AUDIO="$WORK/audio.raw"
mkdir -p "$EXTFS_DIR"
# the movie twice: MoviePlayer's ('TVOD') and SimpleText's ('ttxt')
python3 "$SCRIPT_DIR/guest/install_macbinary.py" "$CACHE/demo-click.bin" "$EXTFS_DIR" Demo >/dev/null
python3 "$SCRIPT_DIR/guest/install_macbinary.py" "$CACHE/demo-click.bin" "$EXTFS_DIR" DemoText >/dev/null
printf 'ttxt' | dd of="$EXTFS_DIR/.finf/DemoText" bs=1 seek=4 conv=notrunc 2>/dev/null
HC_ARGS=()
HC_IMAGE="${MACEMU_IMAGES:-$HOME/storage/images}/dev-tools-system-7.img"
if [[ -f "$HC_IMAGE" && $'\n'"$MOVIES"$'\n' == *$'\n'hc-simple$'\n'* ]]; then
    cp "$HC_IMAGE" "$WORK/hypercard.img"     # HyperCard writes its stacks
    python3 "$SCRIPT_DIR/guest/install_macbinary.py" "$CACHE/hc-simple.bin" "$EXTFS_DIR" "Simple Movie" >/dev/null
    HC_ARGS=(--disk "$WORK/hypercard.img")
fi
cleanup() {
    [[ -n "${EMU_PID:-}" ]] && kill "$EMU_PID" 2>/dev/null && wait "$EMU_PID" 2>/dev/null
    [[ ${EXIT_CODE:-1} -eq 0 ]] && rm -rf "$WORK" || echo "kept: $WORK"
    [[ -z "${MACEMU_DISK:-}" ]] && rm -f "$DISK"
}
trap cleanup EXIT

# behind the web server (not --headless-http): the child's sound to --audio-dump
"$BINARY" --backend executor --config /dev/null --bridge --audio-dump "$AUDIO" --port "$PORT" --timeout 300 \
    --executor-system "$(basename "$DISK")" --executor-writable-images --extfs "$EXTFS_DIR" \
    ${HC_ARGS[@]+"${HC_ARGS[@]}"} >"$LOG" 2>&1 &
EMU_PID=$!

FAIL=0
check() {   # check NAME COMMAND...: PASS/FAIL line
    if "${@:2}"; then echo "PASS: $1"; else echo "FAIL: $1"; FAIL=1; fi
}
api() { curl -sf --max-time 30 "$@"; }
URL="http://localhost:$PORT"

for i in $(seq 1 30); do api "$URL/api/status" >/dev/null && break; sleep 0.5; done
api -X POST "$URL/api/emulator/start" >/dev/null
for i in $(seq 1 90); do
    api "$URL/api/status" | grep -q '"bridge_agent_connected": true' && break
    sleep 1
done

# the movie's rectangle on the screen, "top left bottom right" (find_movie
# SCREENSHOT WIDTH HEIGHT): found by the controller's bar under it, a run of
# black as wide as the movie on two rows 15 apart (its frame), not black
# between them (a black panel on a HyperCard card isn't one). /api/ui
# doesn't answer while MoviePlayer runs.
find_movie() {
    python3 - "$@" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
mw, mh = int(sys.argv[2]), int(sys.argv[3])
w, h = im.size
px = im.load()
black = lambda x, y: px[x, y] == (0, 0, 0)
for y in range(mh, h - 15):
    x = 0
    while x + mw <= w:
        if black(x, y) and black(x + mw - 1, y) and black(x, y + 15) and black(x + mw - 1, y + 15) \
                and all(black(i, y) and black(i, y + 15) for i in range(x, x + mw, 8)) \
                and sum(not black(i, y + 8) for i in range(x, x + mw)) > mw // 2:
            print(y - mh, x, y, x + mw)
            sys.exit(0)
        x += 1
sys.exit(1)
PY
}

# how many pixels of the screen's rectangle differ between two screenshots,
# and whether the rectangle has more than one colour
diff_pixels() {
    python3 - "$@" <<'PY'
import sys
from PIL import Image
a, b = Image.open(sys.argv[1]).convert("RGB"), Image.open(sys.argv[2]).convert("RGB")
t, l, bt, r = map(int, sys.argv[3:7])
box = (l, t, r, bt)
pa, pb = list(a.crop(box).getdata()), list(b.crop(box).getdata())
print(sum(1 for x, y in zip(pa, pb) if x != y), len(set(pa)))
PY
}

# the dump's length so far, in bytes (S16LE stereo at 48 kHz; once the
# sound has started it runs on, silence included)
dump_at() { stat -c %s "$AUDIO" 2>/dev/null || echo 0; }

# seconds of sound in the dump between two of its lengths (heard FROM TO):
# 50 ms windows with signal
heard() {
    python3 - "$AUDIO" "$@" <<'PY'
import struct, sys
a, b = int(sys.argv[2]) // 4 * 4, int(sys.argv[3]) // 4 * 4
try:
    with open(sys.argv[1], "rb") as f:
        f.seek(a)
        data = f.read(max(b - a, 0))
except OSError:
    data = b""
n = len(data) // 4
left = struct.unpack("<%dh" % (n * 2), data[:n * 4])[0::2]
win = 2400
print("%.2f" % (sum(1 for w in range(0, n - win + 1, win)
                    if max(left[w:w + win]) - min(left[w:w + win]) >= 512) / 20.0))
PY
}

click() {   # click X Y
    api -X POST "$URL/api/mouse" -d "{\"x\":$1,\"y\":$2}" >/dev/null
    sleep 0.3
    api -X POST "$URL/api/mouse" -d '{"button":0,"down":true}' >/dev/null
    sleep 0.1
    api -X POST "$URL/api/mouse" -d '{"button":0,"down":false}' >/dev/null
}

# check_movie APP WIDTH HEIGHT [PLAYSECS]: the movie and its controller on
# the screen, then played by a click on the bar's play button (for PLAYSECS
# seconds, default 2: the sound looked for there)
check_movie() {
    local app="$1" width="$2" height="$3" secs="${4:-2}" shot0="$WORK/$1-0.png" shot1="$WORK/$1-1.png"
    local win=""
    for i in $(seq 1 20); do
        api "$URL/api/screenshot?scale=1" -o "$shot0"
        win=$(find_movie "$shot0" "$width" "$height") && break
        sleep 0.5
    done
    [[ -n "$win" ]] || { echo "FAIL: $app: no movie with a controller on the screen ($shot0)"; FAIL=1; return; }
    local mt ml mb mr
    read -r mt ml mb mr <<< "$win"
    check "$app: the movie's window, the controller under the movie at $ml,$mb" true
    read -r _ movie_colours <<< "$(diff_pixels "$shot0" "$shot0" $mt $ml $mb $mr)"
    read -r _ bar_colours <<< "$(diff_pixels "$shot0" "$shot0" $mb $ml $((mb + 16)) $mr)"
    check "$app: the movie drawn ($movie_colours colours)" test "$movie_colours" -gt 1
    check "$app: the controller's bar drawn ($bar_colours colours)" test "$bar_colours" -gt 2
    # play: the second button of the bar (after the speaker)
    local a0 a1
    a0=$(dump_at)
    click $((ml + 23)) $((mb + 8))
    sleep "$secs"
    api "$URL/api/screenshot?scale=1" -o "$shot1"
    a1=$(dump_at)
    read -r changed _ <<< "$(diff_pixels "$shot0" "$shot1" $mt $ml $mb $mr)"
    check "$app: playing changes the movie ($changed pixels)" test "$changed" -gt 100
    local s
    s=$(heard "$a0" "$a1")
    check "$app: its sound heard while it plays ($s s of ${secs} s)" python3 -c "import sys; sys.exit(not $s >= 0.1)"
    MOVIE_BAR="$ml $mb"
}

# check_mute APP: the speaker button mutes the movie (MoviePlayer's volume
# control): from the start again, nothing heard; a second click unmutes it
check_mute() {
    local app="$1" ml mb a0 a1 s
    read -r ml mb <<< "$MOVIE_BAR"
    sleep 2                                 # the movie at its end
    click $((ml + 7)) $((mb + 8))           # the speaker: muted
    sleep 0.5
    a0=$(dump_at)
    click $((ml + 23)) $((mb + 8))          # play from the start
    sleep 2
    a1=$(dump_at)
    s=$(heard "$a0" "$a1")
    check "$app: muted by the speaker, nothing heard ($s s, $(( (a1 - a0) / 192 )) ms of sound out)" \
        python3 -c "import sys; sys.exit(not ($s == 0 and $a1 - $a0 > 192 * 1000))"
    sleep 2
    click $((ml + 7)) $((mb + 8))           # unmuted
    sleep 0.5
    a0=$(dump_at)
    click $((ml + 23)) $((mb + 8))
    sleep 2
    a1=$(dump_at)
    s=$(heard "$a0" "$a1")
    check "$app: unmuted, heard again ($s s)" python3 -c "import sys; sys.exit(not $s >= 0.1)"
}

quit_app() {
    api -X POST "$URL/api/quit" >/dev/null
    api -X POST "$URL/api/wait" -d '{"condition":"app=Finder","timeout":30}' >/dev/null
}

run_app() {   # run_app NAME DOCUMENT
    local app="$1" doc="$2"
    api -X POST "$URL/api/launch" -d "{\"path\":\"Host:$doc\",\"open\":true}" >/dev/null \
        || { echo "FAIL: $app: launch"; FAIL=1; return; }
    api -X POST "$URL/api/wait" -d "{\"condition\":\"app=$app\",\"timeout\":60}" >/dev/null \
        || { echo "FAIL: $app: not in front"; FAIL=1; return; }
    check_movie "$app" 320 176
    [[ "$app" == MoviePlayer ]] && check_mute "$app"
    quit_app
}

run_hypercard() {
    local stack='Development:Languages \u0192:HyperCard 2.4.1 \u0192:HyperCard Stacks:QuickTime Tools'   # JSON escapes
    api -X POST "$URL/api/launch" -d "{\"path\":\"$stack\",\"open\":true}" >/dev/null \
        || { echo "FAIL: HyperCard: launch"; FAIL=1; return; }
    api -X POST "$URL/api/wait" -d '{"condition":"app=HyperCard","timeout":60}' >/dev/null \
        || { echo "FAIL: HyperCard: not in front"; FAIL=1; return; }
    sleep 3
    api -X POST "$URL/api/script" \
        -d '{"creator":"WILD","script":"movie \"Host:Simple Movie\",\"windoid\",\"200,120\""}' >/dev/null \
        || { echo "FAIL: HyperCard: the movie command"; FAIL=1; return; }
    check_movie HyperCard 206 243
    quit_app
}

run_app MoviePlayer Demo
run_app SimpleText DemoText
if [[ ${#HC_ARGS[@]} -gt 0 ]]; then run_hypercard; else echo "SKIP: HyperCard (no dev-tools-system-7.img)"; fi

grep -n "\[unimplemented\].*\(MovieController\|QuickTime selector\)" "$LOG" | head -20
EXIT_CODE=$FAIL
[[ $FAIL -eq 0 ]] && echo "PASS: movies play in MoviePlayer, SimpleText and HyperCard" || echo "FAIL: see above"
exit $FAIL

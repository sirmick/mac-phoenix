#!/bin/bash
#
# refresh_test_disk.sh NAME [OWNER_PID]
#
# A fresh copy of a disk image for a test to boot (and write to), its path
# echoed. The source is the built image NAME.img when provisioning/build.py
# made it (NAME.img.json beside it: tests-7.5.5, tests-7.6.1, 7.5.5 ...;
# built first if it isn't there yet), else the hand-made baseline
# NAME.img.orig (macos-9.0.4, system-6.0.8: the Systems not baked yet).
# Exits 77 (ctest SKIP) if there's no source. --reflink=auto: CoW
# filesystems avoid a real byte copy. The copy's record (.img.json) tags it
# "test" for the web UI's disk list.
#
# With OWNER_PID (the test script's $$) the copy is the test's own,
# ~/storage/images/test-NAME-PID.img, so tests run in parallel (ctest -j)
# don't boot, write or extract the same image: Executor's System Folder is
# extracted per image name (executor-systems/<image>{,.clean}). The copy
# and its extracted System are removed when the owner exits (a detached
# reaper), and copies whose owner died without that are swept by the next
# call. Without OWNER_PID: ~/storage/images/test-NAME.img, kept, for runs
# by hand.
#
# Building and copying the source hold a lock per NAME, so two tests
# starting together don't both rebuild it.
#
# Usage in a test script:
#     SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
#     DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5 $$)}"
#
set -euo pipefail

NAME="${1:?usage: refresh_test_disk.sh NAME [OWNER_PID]}"
OWNER="${2:-}"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
IMAGES="$STORAGE/images"
SYSTEMS="$STORAGE/executor-systems"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -n "$OWNER" ]]; then
    DEST="$IMAGES/test-${NAME}-${OWNER}.img"
else
    DEST="$IMAGES/test-${NAME}.img"
fi

# A copy, its record and Executor's System extracted from it.
remove_copy() {
    local img="$1" base
    base="$(basename "$img")"
    rm -rf "$img" "$img.json" "$SYSTEMS/$base" "$SYSTEMS/$base.clean"
}

# Per-test copies whose test is gone (killed before its reaper ran).
for old in "$IMAGES/test-${NAME}"-[0-9]*.img; do
    [[ -e "$old" ]] || continue
    pid="${old##*-}"; pid="${pid%.img}"
    [[ "$pid" =~ ^[0-9]+$ ]] || continue
    kill -0 "$pid" 2>/dev/null || remove_copy "$old"
done

mkdir -p "$IMAGES"
exec 9>"$IMAGES/.test-${NAME}.lock"
flock 9

if [[ -f "$REPO/provisioning/images/$NAME.json" ]]; then
    # Built from snapshots: (re)build if missing or not up to date.
    python3 "$REPO/provisioning/build.py" "$NAME" >&2 || { echo "refresh_test_disk: build of $NAME failed" >&2; exit 77; }
    SRC="$IMAGES/$NAME.img"
else
    SRC="$IMAGES/$NAME.img.orig"
fi

if [[ ! -f "$SRC" ]]; then
    echo "refresh_test_disk: no source: $SRC" >&2
    exit 77
fi

remove_copy "$DEST"
cp --reflink=auto -f "$SRC" "$DEST"
flock -u 9
exec 9>&-
python3 - "$IMAGES/$NAME.img.json" "$DEST.json" "$NAME" <<'PY'
import json, sys
src, dest, name = sys.argv[1:]
try:
    rec = json.load(open(src))
except (OSError, ValueError):
    rec = {}
tags = ["test"] + [t for t in rec.get("tags", []) if t in ("68k", "ppc")]
json.dump({"title": "Test run copy of " + rec.get("title", name), "tags": tags, "copy_of": name},
          open(dest, "w"), indent=1, ensure_ascii=False)
PY
if [[ -n "$OWNER" ]]; then
    # Detached, with no hold on our stdout (the caller reads it in $(...)).
    setsid bash -c '
        while kill -0 "$1" 2>/dev/null; do sleep 2; done
        img="$2"; base="$(basename "$img")"
        rm -rf "$img" "$img.json" "$3/$base" "$3/$base.clean"
    ' reaper "$OWNER" "$DEST" "$SYSTEMS" </dev/null >/dev/null 2>&1 &
fi
echo "$DEST"

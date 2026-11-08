#!/bin/bash
#
# disk_movies.sh — the movies on our disks (docs/executor/QuickTime.md
# section 6), as MacBinary files in a cache, for run_qt.sh --disk:
#
#   tests/guest/qt/disk_movies.sh [CACHE]      (default ~/storage/qt/disk-movies)
#
# Each is taken out of its image (~/storage/images, read from a scratch
# copy: hfsutils may write the volume it mounts) once; a movie whose image
# is missing is left out. Prints the names present, one per line.
#
set -uo pipefail

CACHE="${1:-${QT_STORE:-$HOME/storage/qt}/disk-movies}"
IMAGES="${MACEMU_IMAGES:-$HOME/storage/images}"
mkdir -p "$CACHE"

# name, image, path on it (MacRoman: \xc4 is ƒ)
MOVIES=(
    "demo-click	7.6.1.img	:System Folder:Extensions:Demo click&drag"
    "demo-lock	7.6.1.img	:System Folder:Extensions:Demo drag/lock"
    "demo-single	7.6.1.img	:System Folder:Extensions:Demo single.click"
    "hc-simple	dev-tools-system-7.img	:Languages \xc4:HyperCard 2.4.1 \xc4:HyperCard Stacks:Simple Movie"
    "ares-1	games.img	:Ares 1.2.0 \xc4:Ares Data \xc4:Part 1"
    "ares-2	games.img	:Ares 1.2.0 \xc4:Ares Data \xc4:Part 2"
    "ares-3	games.img	:Ares 1.2.0 \xc4:Ares Data \xc4:Part 3"
)

missing=()
for entry in "${MOVIES[@]}"; do
    IFS=$'\t' read -r name image path <<< "$entry"
    [[ -s "$CACHE/$name.bin" ]] || missing+=("$entry")
done

if [[ ${#missing[@]} -gt 0 ]]; then
    command -v hcopy > /dev/null || { echo "disk_movies.sh: no hfsutils (hcopy)" >&2; missing=(); }
    SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/qtdisk.XXXXXX")"
    trap 'rm -rf "$SCRATCH"' EXIT
    copied=""
    for entry in ${missing[@]+"${missing[@]}"}; do
        IFS=$'\t' read -r name image path <<< "$entry"
        [[ -f "$IMAGES/$image" ]] || continue
        if [[ "$copied" != "$image" ]]; then
            rm -f "$SCRATCH"/*.img
            cp "$IMAGES/$image" "$SCRATCH/$image" || continue
            copied="$image"
        fi
        # hfsutils keeps its current volume in $HOME/.hcwd: a private HOME
        HOME="$SCRATCH" hmount "$SCRATCH/$image" > /dev/null 2>&1 &&
            HOME="$SCRATCH" hcopy -m "$(printf "$path")" "$CACHE/$name.bin.part" 2> /dev/null &&
            mv "$CACHE/$name.bin.part" "$CACHE/$name.bin"
        rm -f "$CACHE/$name.bin.part"
    done
fi

for entry in "${MOVIES[@]}"; do
    IFS=$'\t' read -r name image path <<< "$entry"
    [[ -s "$CACHE/$name.bin" ]] && echo "$name"
done
exit 0

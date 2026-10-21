#!/bin/bash
# executor-upstream-diff.sh — MacPhoenix's Executor against the upstream
# commit it was imported from (docs/executor/README.md).
#
#   tools/executor-upstream-diff.sh            summary: files new/removed/changed
#   tools/executor-upstream-diff.sh --patch    unified diff on stdout
#
# Upstream's src/ is our romlib/; multiversal/ and tests/ keep their names.
# Needs the upstream checkout in executor/ (gitignored; cloned on first use)
# with the multiversal submodule.
set -euo pipefail

BASE=b2569eb1                 # docs/executor/PLAN.md: the import
REPO="$(cd "$(dirname "$0")/.." && pwd)"
UP="$REPO/executor"
OURS="$REPO/src/executor"

if [[ ! -d "$UP/.git" ]]; then
    git clone --quiet https://github.com/autc04/executor.git "$UP"
fi
git -C "$UP" submodule update --init --quiet multiversal

TMP="$(mktemp -d "${TMPDIR:-/tmp}/executor-base.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
git -C "$UP" archive "$BASE" src tests | tar -x -C "$TMP"
mv_commit=$(git -C "$UP" ls-tree "$BASE" multiversal | awk '{print $3}')
mkdir -p "$TMP/multiversal"
git -C "$UP/multiversal" archive "$mv_commit" | tar -x -C "$TMP/multiversal"

pairs=("src:romlib" "multiversal:multiversal" "tests:tests")

if [[ "${1:-}" == "--patch" ]]; then
    for p in "${pairs[@]}"; do
        diff -ruN --label "upstream/${p%%:*}" --label "macphoenix/${p#*:}" \
            "$TMP/${p%%:*}" "$OURS/${p#*:}" || true
    done
    exit 0
fi

for p in "${pairs[@]}"; do
    up="$TMP/${p%%:*}" ours="$OURS/${p#*:}"
    (cd "$up" && find . -type f | sort) > "$TMP/up.txt"
    (cd "$ours" && find . -type f | sort) > "$TMP/ours.txt"
    new=$(comm -13 "$TMP/up.txt" "$TMP/ours.txt" | wc -l)
    gone=$(comm -23 "$TMP/up.txt" "$TMP/ours.txt" | wc -l)
    changed=0
    while read -r f; do
        cmp -s "$up/$f" "$ours/$f" || changed=$((changed + 1))
    done < <(comm -12 "$TMP/up.txt" "$TMP/ours.txt")
    added=$(diff -ruN "$up" "$ours" | grep -c '^+[^+]' || true)
    removed=$(diff -ruN "$up" "$ours" | grep -c '^-[^-]' || true)
    printf '%-12s new %3d  removed %3d  changed %3d  lines +%d -%d\n' \
        "${p#*:}/" "$new" "$gone" "$changed" "$added" "$removed"
done
echo "outside upstream's tree: src/executor/cpu/ (syn68k facade, PowerCore on mame-ppc), host/"

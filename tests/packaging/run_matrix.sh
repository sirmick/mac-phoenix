#!/bin/bash
#
# run_matrix.sh — build the source tarball, then build + boot-test the
# native package on each target distro inside Docker.
#
# Targets are simple "tag:base-image" pairs, dispatched by extension to
# Dockerfile.deb (apt-based) or Dockerfile.rpm (dnf-based).
#
# Boot test: bind-mounts a host ROM and disk image into the container at
# fixed paths and runs:
#     mac-phoenix --backend uae --no-webserver --network none --timeout 60 \
#         --rom /storage/rom --disk /storage/disk.img
# Pass success: emulator stderr contains "Desktop ready".
#
# Skip boot test with MACEMU_SKIP_BOOT=1 (smoke checks still run as part of
# the Docker build).
#
# Env overrides:
#   MACEMU_ROM       default: $HOME/quadra.rom
#   MACEMU_DISK      default: $HOME/storage/images/macos-7.5.5.img
#   MACEMU_TIMEOUT   default: 60   (seconds inside the container)
#   MACEMU_TARGETS   default: ubuntu-24.04 ubuntu-22.04 debian-12 fedora-40

set -euo pipefail

docker info >/dev/null 2>&1 || { echo "docker not usable — install docker.io and add yourself to the docker group (newgrp docker)" >&2; exit 1; }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

ROM="${MACEMU_ROM:-$HOME/quadra.rom}"
DISK="${MACEMU_DISK:-$HOME/storage/images/macos-7.5.5.img}"
TIMEOUT="${MACEMU_TIMEOUT:-60}"
TARGETS="${MACEMU_TARGETS:-ubuntu-24.04 ubuntu-22.04 debian-12 fedora-40}"

declare -A BASE=(
    [ubuntu-24.04]=ubuntu:24.04
    [ubuntu-22.04]=ubuntu:22.04
    [debian-12]=debian:12
    [fedora-40]=fedora:40
)

# tarball
echo "[matrix] producing source tarball..."
"$ROOT/tools/make-source-tarball.sh" >/dev/null
TARBALL="$(ls -1t "$ROOT/dist"/mac-phoenix_*.tar.xz | head -1)"
echo "[matrix] tarball: $TARBALL"

# build context for docker (single dir keeps `docker build` send-size small)
CTX="$ROOT/tests/packaging/build-ctx"
rm -rf "$CTX"; mkdir -p "$CTX"
cp "$TARBALL" "$CTX/"
cp "$ROOT/tests/packaging/Dockerfile.deb" "$CTX/"
cp "$ROOT/tests/packaging/Dockerfile.rpm" "$CTX/"
cp "$ROOT/rpm/mac-phoenix.spec" "$CTX/"

results=()

for target in $TARGETS; do
    base="${BASE[$target]:-}"
    if [[ -z "$base" ]]; then
        echo "[matrix] unknown target: $target" >&2
        results+=("$target: UNKNOWN_TARGET")
        continue
    fi

    case "$base" in
        fedora:*|rocky:*|alma:*) dockerfile=Dockerfile.rpm ;;
        *)                       dockerfile=Dockerfile.deb ;;
    esac

    image="mac-phoenix-pkg:$target"
    log="$ROOT/dist/$target.log"

    echo
    echo "============================================================"
    echo "[matrix] $target  ($base, $dockerfile)"
    echo "============================================================"

    if ! docker build \
            --build-arg "BASE=$base" \
            -f "$CTX/$dockerfile" \
            -t "$image" \
            "$CTX" 2>&1 | tee "$log"; then
        results+=("$target: BUILD_FAIL  (see $log)")
        continue
    fi

    if [[ "${MACEMU_SKIP_BOOT:-0}" = "1" ]]; then
        results+=("$target: BUILD_OK  (boot skipped)")
        continue
    fi

    if [[ ! -f "$ROM" || ! -f "$DISK" ]]; then
        results+=("$target: BUILD_OK  (boot skipped — ROM/disk missing: $ROM / $DISK)")
        continue
    fi

    boot_log="$ROOT/dist/$target.boot.log"
    echo "[matrix] booting in $target (rom=$(basename "$ROM") disk=$(basename "$DISK") timeout=${TIMEOUT}s)"
    # Bind-mount ROM and disk individually so any host path works; the emulator
    # may write to the disk image so it's mounted RW. Emulator exits with the
    # --timeout signal so we treat exit status as advisory and grep the log.
    docker run --rm \
            -v "$ROM":/storage/rom:ro \
            -v "$DISK":/storage/disk.img \
            "$image" \
            mac-phoenix \
                --backend uae \
                --no-webserver \
                --network none \
                --timeout "$TIMEOUT" \
                --rom /storage/rom \
                --disk /storage/disk.img \
            2>&1 | tee "$boot_log" || true

    if grep -q "Desktop ready" "$boot_log"; then
        results+=("$target: OK (boot reached Desktop)")
    else
        results+=("$target: BOOT_FAIL  (see $boot_log)")
    fi
done

echo
echo "============================================================"
echo "RESULTS"
echo "============================================================"
printf '  %s\n' "${results[@]}"

# Non-zero exit if any cell failed
fail=0
for r in "${results[@]}"; do
    [[ "$r" == *"FAIL"* || "$r" == *"UNKNOWN"* ]] && fail=1
done
exit $fail

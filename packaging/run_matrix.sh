#!/bin/bash
#
# run_matrix.sh — build the source tarball, then build + boot-test the
# native package on each target distro inside Docker.
#
# Targets are simple "tag:base-image" pairs, dispatched by extension to
# Dockerfile.deb (apt-based) or Dockerfile.rpm (dnf-based). BridgeAgent.bin
# is shipped as a committed artifact (BUILD_BRIDGE_AGENT=OFF in package
# builds), so no Retro68 toolchain or Apple UI 3.4 is needed here. To
# rebuild BridgeAgent, see packaging/Dockerfile.dev.
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
#   MACEMU_SKIP_BOOT          skip the boot test (artifact-only run)
#   MACEMU_KEEP_IMAGES        keep mac-phoenix-pkg:* images post-extraction

set -euo pipefail

docker info >/dev/null 2>&1 || { echo "docker not usable — install docker.io and add yourself to the docker group (newgrp docker)" >&2; exit 1; }

# BuildKit needs the buildx plugin (not bundled with Ubuntu's docker.io).
# Stick to the legacy builder; the EXIT trap below handles cleanup.
export DOCKER_BUILDKIT=0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Sweep any orphaned exited build containers from previous (failed) runs
# and on EXIT — legacy docker build leaves them behind when a step fails.
cleanup_docker() {
    docker container prune -f >/dev/null 2>&1 || true
}
trap cleanup_docker EXIT

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

# --- Source tarball ------------------------------------------------------
echo "[matrix] producing source tarball..."
mkdir -p "$ROOT/dist"
"$ROOT/tools/make-source-tarball.sh" >/dev/null
TARBALL="$(ls -1t "$ROOT/dist"/mac-phoenix_*.tar.xz | head -1)"
echo "[matrix] tarball: $TARBALL"

# Single build context dir keeps `docker build` send-size small.
CTX="$ROOT/packaging/build-ctx"
rm -rf "$CTX"; mkdir -p "$CTX"
cp "$TARBALL" "$CTX/"
cp "$ROOT/packaging/Dockerfile.deb" "$CTX/"
cp "$ROOT/packaging/Dockerfile.rpm" "$CTX/"
cp "$ROOT/rpm/mac-phoenix.spec" "$CTX/"

results=()

# --- Per-target build ----------------------------------------------------
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

    # Extract built .deb / .rpm out of the test image to dist/packages/<target>/
    pkg_dir="$ROOT/dist/packages/$target"
    rm -rf "$pkg_dir"; mkdir -p "$pkg_dir"
    cid="$(docker create "$image")"
    docker cp "$cid:/pkg/." "$pkg_dir/" >/dev/null 2>&1 || true
    docker rm "$cid" >/dev/null
    artifact="$(find "$pkg_dir" -maxdepth 1 \( -name '*.deb' -o -name '*.rpm' \) | head -1)"
    if [[ -n "$artifact" ]]; then echo "[matrix] artifact: $artifact"; fi

    # Boot test (uses the image, so must run BEFORE we rmi it).
    if [[ "${MACEMU_SKIP_BOOT:-0}" = "1" ]]; then
        results+=("$target: BUILD_OK  (boot skipped)")
    elif [[ ! -f "$ROM" || ! -f "$DISK" ]]; then
        results+=("$target: BUILD_OK  (boot skipped — ROM/disk missing: $ROM / $DISK)")
    else
        boot_log="$ROOT/dist/$target.boot.log"
        echo "[matrix] booting in $target (rom=$(basename "$ROM") disk=$(basename "$DISK") timeout=${TIMEOUT}s)"
        # Disk mounted RW because the emulator may write to it; ROM is RO.
        # Emulator exits with the --timeout signal so we grep the log.
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
    fi

    # Drop the test image now that we're done with it. ~150-400MB each;
    # keeping them all adds up fast. Set MACEMU_KEEP_IMAGES=1 to retain
    # for debugging.
    if [[ "${MACEMU_KEEP_IMAGES:-0}" != "1" ]]; then
        docker rmi "$image" >/dev/null 2>&1 || true
    fi
done

# --- Results -------------------------------------------------------------
echo
echo "============================================================"
echo "RESULTS"
echo "============================================================"
printf '  %s\n' "${results[@]}"
echo
echo "Artifacts under: $ROOT/dist/packages/"
ls -1 "$ROOT/dist/packages/"/*/*.deb "$ROOT/dist/packages/"/*/*.rpm 2>/dev/null | sed 's|^|  |' || true

# Non-zero exit if any cell failed
fail=0
for r in "${results[@]}"; do
    [[ "$r" == *"FAIL"* || "$r" == *"UNKNOWN"* ]] && fail=1
done
exit $fail

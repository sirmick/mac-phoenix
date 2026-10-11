#!/bin/bash
#
# xarch.sh - build and test MacPhoenix as an arm64 or riscv64 program on this
# x86-64 box, in a Debian container run through qemu-user (binfmt_misc).
#
# Usage:
#   tools/xarch/xarch.sh ARCH                 configure + build in build-ARCH/
#   tools/xarch/xarch.sh ARCH ctest -R cpu_cores
#   tools/xarch/xarch.sh ARCH shell           interactive shell in the container
#   tools/xarch/xarch.sh ARCH CMD [ARGS...]   any command, in the repo
#
# ARCH is arm64 or riscv64. The image (mac-phoenix-xarch:ARCH) is built from
# tools/xarch/Dockerfile on first use and rebuilt when it changes. The repo,
# ~/roms, ~/storage and ~/src/mac-phoenix-assets are mounted at their own paths, so the test scripts
# find ROMs and disk images as on the host; HOME inside is build-ARCH/home
# (cargo cache, mac-phoenix config). Everything runs as the calling user.
#
# Needs: docker, and qemu registered with binfmt_misc for the architecture
# (`arch-test` lists it). Expect native builds to run several times slower
# than on the host.
#
set -euo pipefail

ARCH="${1:?usage: xarch.sh arm64|riscv64 [command...]}"
shift
case "$ARCH" in
    arm64|riscv64) ;;
    *) echo "xarch: unknown architecture '$ARCH' (arm64 | riscv64)" >&2; exit 2 ;;
esac

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$REPO/build-$ARCH"
IMAGE="mac-phoenix-xarch:$ARCH"

# Docker access: re-run under the docker group when this session predates
# being added to it.
if ! docker info >/dev/null 2>&1; then
    if [[ -z "${XARCH_SG:-}" ]] && grep -qw "$(id -un)" <<<"$(getent group docker)"; then
        exec env XARCH_SG=1 sg docker -c "$(printf '%q ' "$0" "$ARCH" "$@")"
    fi
    echo "xarch: cannot reach docker" >&2
    exit 1
fi

if ! grep -qx "$ARCH" <<<"$(arch-test 2>/dev/null)"; then
    echo "xarch: $ARCH binaries do not run here (qemu not registered with binfmt_misc)" >&2
    exit 1
fi

# --load: the default builder may be a docker-container one (buildx), whose
# results stay in its cache unless loaded into the image store.
docker buildx build -q --load --platform "linux/$ARCH" -t "$IMAGE" "$REPO/tools/xarch" >/dev/null

mkdir -p "$BUILD/home"
MOUNTS=(-v "$REPO:$REPO" -v "$BUILD/home:$HOME")
# ~/storage links into mac-phoenix-assets (ROMs, images), so mount it too.
for d in "$HOME/roms" "$HOME/storage" "$HOME/src/mac-phoenix-assets"; do
    [[ -d "$d" ]] && MOUNTS+=(-v "$d:$d")
done

# Without Qt WebEngine (riscv64) the MacBrowser host pipeline is left out.
CONFIGURE="cmake -S '$REPO' -B '$BUILD' -DCMAKE_BUILD_TYPE=RelWithDebInfo"
CONFIGURE+=" \$( [ -e /usr/lib/*/cmake/Qt6WebEngineCore ] || echo -DBUILD_BROWSER=OFF )"

if [[ $# -eq 0 ]]; then
    # Half the cores: compilers under qemu need several times their native
    # memory, and -j$(nproc) ran this 20 GB box out of it.
    set -- sh -c "$CONFIGURE && cmake --build '$BUILD' -j$(( ($(nproc) + 1) / 2 ))"
elif [[ "$1" == "shell" ]]; then
    set -- bash
elif [[ "$1" == "ctest" ]]; then
    shift
    set -- ctest --test-dir "$BUILD" "$@"
fi

TTY=()
[[ -t 0 && -t 1 ]] && TTY=(-it)

exec docker run --rm --init "${TTY[@]}" --platform "linux/$ARCH" \
    --user "$(id -u):$(id -g)" -e HOME="$HOME" -e MACEMU_XARCH="$ARCH" \
    --network host --security-opt seccomp=unconfined \
    "${MOUNTS[@]}" -w "$REPO" "$IMAGE" "$@"

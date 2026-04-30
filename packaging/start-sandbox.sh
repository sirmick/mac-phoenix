#!/usr/bin/env bash
# start-sandbox.sh — build + run + drop into the Ubuntu 24.04 sandbox
# for testing built packages from dist/.

set -euo pipefail

IMAGE="mp-sandbox"
CONTAINER="mp-sandbox"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build the image if it's missing (or if Dockerfile is newer than the image).
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "[sandbox] building image $IMAGE..."
    docker build -t "$IMAGE" \
        --build-arg "UID=$(id -u)" \
        --build-arg "GID=$(id -g)" \
        -f "$SCRIPT_DIR/Dockerfile.sandbox" \
        "$SCRIPT_DIR"
fi

# Start the container if not already running.
if ! docker container inspect "$CONTAINER" >/dev/null 2>&1; then
    echo "[sandbox] starting container $CONTAINER..."
    docker run -d --name "$CONTAINER" --network host \
        -v "$HOME/storage:/home/mick/storage" \
        -v "$HOME/mac-phoenix/dist:/home/mick/dist" \
        "$IMAGE" sleep infinity >/dev/null
elif [ "$(docker container inspect -f '{{.State.Running}}' "$CONTAINER")" != "true" ]; then
    echo "[sandbox] container exists but is stopped — restarting..."
    docker start "$CONTAINER" >/dev/null
fi

echo "[sandbox] entering shell — exit to leave (container keeps running)."
exec docker exec -it -u mick "$CONTAINER" bash

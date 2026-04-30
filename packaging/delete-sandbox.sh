#!/usr/bin/env bash
# delete-sandbox.sh — tear down the sandbox container + image.

set -euo pipefail

IMAGE="mp-sandbox"
CONTAINER="mp-sandbox"

if docker container inspect "$CONTAINER" >/dev/null 2>&1; then
    echo "[sandbox] removing container $CONTAINER..."
    docker rm -f "$CONTAINER" >/dev/null
fi

if docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "[sandbox] removing image $IMAGE..."
    docker rmi "$IMAGE" >/dev/null
fi

echo "[sandbox] gone."

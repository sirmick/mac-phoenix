#!/bin/bash
# Quick test script for macemu-next with UAE backend
# Usage: ./run_traces.sh [timeout_seconds]

TIMEOUT=${1:-5}

echo "========================================="
echo "Running macemu-next with UAE backend"
echo "Timeout: ${TIMEOUT} seconds"
echo "========================================="

env CPU_BACKEND=uae EMULATOR_TIMEOUT=${TIMEOUT} \
    ./build/macemu-next --config ./trace-config.json

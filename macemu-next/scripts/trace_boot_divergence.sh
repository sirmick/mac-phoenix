#!/bin/bash
# Trace boot sequence to find divergence point

cd /home/mick/macemu-dual-cpu/macemu-next/build

echo "=== Unicorn boot sequence ==="
EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn ./macemu-next --no-webserver 2>&1 | \
    grep -E "7106|7107|7128|PATCH|A05D|SCSI|WLSC|Non-EmulOp" | \
    head -20

echo ""
echo "=== UAE boot sequence ==="
EMULATOR_TIMEOUT=2 CPU_BACKEND=uae ./macemu-next --no-webserver 2>&1 | \
    grep -E "7106|7107|7128|PATCH|A05D|SCSI|WLSC" | \
    head -20
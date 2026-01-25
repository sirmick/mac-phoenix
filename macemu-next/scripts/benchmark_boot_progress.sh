#!/bin/bash
# Benchmark how long each backend takes to reach boot milestones

echo "=========================================="
echo "Boot Progress Benchmarking"
echo "=========================================="
echo ""

# Function to measure time to milestone
measure_milestone() {
    local backend=$1
    local milestone=$2
    local max_time=${3:-60}

    echo -n "$backend to $milestone: "

    start_time=$(date +%s.%N)

    # Run until milestone is reached or timeout
    EMULATOR_TIMEOUT=$max_time CPU_BACKEND=$backend EMULOP_VERBOSE=1 \
        ./build/macemu-next --no-webserver 2>&1 | \
        grep -q "$milestone"

    if [ ${PIPESTATUS[1]} -eq 0 ]; then
        end_time=$(date +%s.%N)
        elapsed=$(echo "$end_time - $start_time" | bc)
        echo "$elapsed seconds"
    else
        echo "Not reached in ${max_time}s"
    fi
}

# Key milestones to measure
milestones=(
    "RESET"
    "READ_XPRAM2"
    "PATCH_BOOT_GLOBS"
    "INSTIME"
    "SCSI_DISPATCH"
)

echo "=== Time to Milestones ==="
echo ""

for milestone in "${milestones[@]}"; do
    echo "--- $milestone ---"
    measure_milestone "uae" "$milestone" 10
    measure_milestone "unicorn" "$milestone" 60
    echo ""
done

echo "=== EmulOp Frequency Analysis ==="
echo ""

# Count EmulOps in first 10 seconds
echo "EmulOps executed in 10 seconds:"
UAE_COUNT=$(EMULATOR_TIMEOUT=10 CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | grep -c "Executing 0x")
echo "  UAE:     $UAE_COUNT ($(( UAE_COUNT / 10 ))/sec)"

UNICORN_COUNT=$(EMULATOR_TIMEOUT=10 CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | grep -c "Executing 0x")
echo "  Unicorn: $UNICORN_COUNT ($(( UNICORN_COUNT / 10 ))/sec)"

echo ""
echo "Performance ratio: $(echo "scale=2; $UAE_COUNT / $UNICORN_COUNT" | bc)x"
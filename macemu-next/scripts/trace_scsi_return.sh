#!/bin/bash
# Trace execution after SCSI_DISPATCH returns
# We know it returns to PC 0x020052a4

cd /home/mick/macemu-dual-cpu/macemu-next/build

echo "=== Tracing UAE after SCSI_DISPATCH ==="
# Start trace at return address minus a few instructions for context
# 0x020052a4 = 33637028 decimal
# Start at 0x02005290 = 33637008 decimal (20 bytes before)
# End at 0x02005300 = 33637120 decimal (92 bytes after)
START=$((0x02005290))
END=$((0x02005300))
EMULATOR_TIMEOUT=2 CPU_BACKEND=uae CPU_TRACE=$START-$END ./macemu-next --no-webserver 2>&1 | grep -E "(SCSI_DISPATCH|PC=0200529|PC=020052a|PC=020052b|PC=020052c|PC=020052d|PC=020052e|PC=020052f|PC=02005300)" | head -100

echo ""
echo "=== Tracing Unicorn after SCSI_DISPATCH ==="
EMULATOR_TIMEOUT=2 CPU_BACKEND=unicorn CPU_TRACE=$START-$END ./macemu-next --no-webserver 2>&1 | grep -E "(SCSI_DISPATCH|PC=0200529|PC=020052a|PC=020052b|PC=020052c|PC=020052d|PC=020052e|PC=020052f|PC=02005300)" | head -100
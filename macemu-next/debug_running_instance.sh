#!/bin/bash
# Debug script for running macemu-next instance

PID=$(pgrep -f "macemu-next" | head -1)

if [ -z "$PID" ]; then
    echo "No macemu-next process found"
    exit 1
fi

echo "=== macemu-next Process Info ==="
echo "PID: $PID"
ps -p $PID -o pid,ppid,%cpu,%mem,etime,cmd

echo ""
echo "=== Network Connections ==="
lsof -i -P -n 2>/dev/null | grep $PID

echo ""
echo "=== CPU State (last 10 seconds) ==="
for i in {1..10}; do
    CPU=$(ps -p $PID -o %cpu= 2>/dev/null)
    echo "  $i sec: CPU=$CPU%"
    sleep 1
done

echo ""
echo "=== Memory Map (looking for framebuffer) ==="
cat /proc/$PID/maps 2>/dev/null | grep -E "rw-p.*\[heap\]|rw-p.*anon" | head -5

echo ""
echo "=== Open Files ==="
ls -l /proc/$PID/fd 2>/dev/null | wc -l
echo "   file descriptors open"

echo ""
echo "=== Try HTTP API ==="
curl -s -m 2 http://localhost:8000/api/status 2>&1 | head -20

echo ""
echo "=== Send SIGUSR1 to trigger debug output (if supported) ==="
kill -USR1 $PID 2>/dev/null && echo "Signal sent" || echo "Could not send signal"

#!/bin/bash
set -e

echo "=========================================="
echo "Starting Bambu P1 Streamer Container"
echo "=========================================="
echo "Printer Address: ${PRINTER_ADDRESS}"
echo "HTTP Status Port: ${HTTP_PORT:-8081}"
echo "=========================================="

# Clear any old status file
rm -f /tmp/bambu_stream_status

# Start StatusServer in the background
echo "Starting HTTP Status Server on port ${HTTP_PORT:-8081}..."
./StatusServer "${HTTP_PORT:-8081}" 2>&1 &
STATUS_PID=$!

# Give it a moment to start
sleep 1

# Check if StatusServer is still running
if ! kill -0 $STATUS_PID 2>/dev/null; then
    echo "ERROR: StatusServer failed to start"
    exit 1
fi

echo "StatusServer started successfully (PID: $STATUS_PID)"
echo "=========================================="
echo "Note: BambuP1Streamer will start automatically"
echo "when go2rtc receives a stream request."
echo "=========================================="

# Start go2rtc in the foreground (this keeps the container running)
echo "Starting go2rtc..."
exec ./go2rtc_linux_amd64

#!/bin/bash
set -e

echo "=========================================="
echo "Starting Bambu P1 Streamer Container"
echo "=========================================="
echo "Printer Address: ${PRINTER_ADDRESS}"
echo "HTTP Port: ${HTTP_PORT:-8081}"
echo "=========================================="

# Start BambuP1Streamer in the background with HTTP server
echo "Starting BambuP1Streamer with HTTP server..."
./BambuP1Streamer ./libBambuSource.so "${PRINTER_ADDRESS}" "${PRINTER_ACCESS_CODE}" "${HTTP_PORT:-8081}" 2>&1 &
BAMBU_PID=$!

# Give it a moment to start and output initial logs
sleep 2

# Check if BambuP1Streamer is still running
if ! kill -0 $BAMBU_PID 2>/dev/null; then
    echo "ERROR: BambuP1Streamer failed to start or exited immediately"
    exit 1
fi

echo "BambuP1Streamer started successfully (PID: $BAMBU_PID)"
echo "=========================================="

# Start go2rtc in the foreground (this keeps the container running)
echo "Starting go2rtc..."
exec ./go2rtc_linux_amd64

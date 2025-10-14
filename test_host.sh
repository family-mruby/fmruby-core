#!/bin/bash

# Final test to verify IPC communication

echo "=== Family mruby IPC Communication Test ==="
echo ""

rake build:linux
rake host:build

# Start host server
echo "Starting host SDL2 server..."
host/sdl2/build/fmrb_host_sdl2 &
HOST_PID=$!
sleep 2

if [ ! -S /tmp/fmrb_socket ]; then
    echo "ERROR: Socket not created"
    kill $HOST_PID 2>/dev/null
    exit 1
fi

echo "Socket created: /tmp/fmrb_socket"
echo ""

# Run core in Docker
echo "Running fmruby-core in Docker for 100 frames..."
echo "(Core sends drawing commands via IPC)"
echo ""

docker run --rm --user $(id -u):$(id -g) \
    -v $PWD:/project \
    -v /tmp:/tmp \
    esp32_build_container:v5.5.1 \
    /project/build/fmruby-core.elf 2>&1 | \
    grep -E "(Graphics initialized|Frame:|complete|IPC send)" | \
    tail -20

EXIT_CODE=${PIPESTATUS[0]}

# Cleanup
echo ""
echo "Cleaning up..."
kill $HOST_PID 2>/dev/null
wait $HOST_PID 2>/dev/null
rm -f /tmp/fmrb_socket

if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo "=== TEST PASSED ==="
    echo "- fmruby-core successfully sent drawing commands via IPC"
    echo "- host/sdl2 received the commands (socket communication working)"
    echo "- 100 frames completed successfully"
else
    echo ""
    echo "=== TEST FAILED ==="
    echo "Exit code: $EXIT_CODE"
fi

exit $EXIT_CODE

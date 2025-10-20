#!/bin/bash

# Check if SDL2 host is already running
if [ ! -S /tmp/fmrb_socket ]; then
    echo "Starting SDL2 host process..."

    # Check if host binary exists
    if [ ! -f host/sdl2/build/fmrb_host_sdl2 ]; then
        echo "ERROR: SDL2 host binary not found. Run 'rake host:build' first."
        exit 1
    fi

    # Start host in background
    host/sdl2/build/fmrb_host_sdl2 &
    HOST_PID=$!

    # Wait for socket creation with retry
    echo "Waiting for socket creation..."
    RETRY_COUNT=0
    MAX_RETRIES=20
    while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
        if [ -S /tmp/fmrb_socket ]; then
            echo "Socket created: /tmp/fmrb_socket"
            break
        fi
        sleep 0.5
        RETRY_COUNT=$((RETRY_COUNT + 1))
    done

    # Verify socket creation
    if [ ! -S /tmp/fmrb_socket ]; then
        echo "ERROR: Failed to create socket after ${MAX_RETRIES} retries (10 seconds)"
        kill $HOST_PID 2>/dev/null
        exit 1
    fi
else
    echo "SDL2 host already running (socket exists)"
fi

# Cleanup on exit
cleanup() {
    if [ ! -z "$HOST_PID" ]; then
        echo ""
        echo "Stopping host process..."
        kill $HOST_PID 2>/dev/null
        wait $HOST_PID 2>/dev/null
        rm -f /tmp/fmrb_socket
    fi
}
trap cleanup EXIT INT TERM

# Run FMRuby Core in Docker
echo "Starting FMRuby Core..."
docker run --rm --user $(id -u):$(id -g) \
    -v $PWD:/project \
    -v /tmp:/tmp \
    esp32_build_container:v5.5.1 \
    /project/build/fmruby-core.elf

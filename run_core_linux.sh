#!/bin/bash

# Start socat for FS proxy UART
echo "Starting socat for FS proxy UART..."
socat -d -d pty,raw,echo=0,link=/tmp/fmrb_uart_core pty,raw,echo=0,link=/tmp/fmrb_uart_host 2>&1 | tee /tmp/socat.log &
SOCAT_PID=$!

# Wait for PTY creation
echo "Waiting for PTY creation..."
RETRY_COUNT=0
MAX_RETRIES=10
while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    if [ -L /tmp/fmrb_uart_core ] && [ -L /tmp/fmrb_uart_host ]; then
        UART_CORE=$(readlink -f /tmp/fmrb_uart_core)
        UART_HOST=$(readlink -f /tmp/fmrb_uart_host)
        echo "PTY created:"
        echo "  Core side: ${UART_CORE} -> /tmp/fmrb_uart_core"
        echo "  Host side: ${UART_HOST} -> /tmp/fmrb_uart_host"
        break
    fi
    sleep 0.2
    RETRY_COUNT=$((RETRY_COUNT + 1))
done

if [ ! -L /tmp/fmrb_uart_core ] || [ ! -L /tmp/fmrb_uart_host ]; then
    echo "ERROR: Failed to create PTY after ${MAX_RETRIES} retries"
    kill $SOCAT_PID 2>/dev/null
    exit 1
fi

# Check if SDL2 host is already running
if [ ! -S /tmp/fmrb_socket ]; then
    echo "Starting SDL2 host process..."

    # Check if host binary exists
    if [ ! -f host/sdl2/build/fmrb_host_sdl2 ]; then
        echo "ERROR: SDL2 host binary not found. Run 'rake host:build' first."
        kill $SOCAT_PID 2>/dev/null
        rm -f /tmp/fmrb_uart_core /tmp/fmrb_uart_host
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
        kill $SOCAT_PID 2>/dev/null
        rm -f /tmp/fmrb_uart_core /tmp/fmrb_uart_host
        exit 1
    fi
else
    echo "SDL2 host already running (socket exists)"
fi

# Cleanup on exit
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ ! -z "$HOST_PID" ]; then
        echo "Stopping host process..."
        kill $HOST_PID 2>/dev/null
        wait $HOST_PID 2>/dev/null
        rm -f /tmp/fmrb_socket
    fi
    if [ ! -z "$SOCAT_PID" ]; then
        echo "Stopping socat..."
        kill $SOCAT_PID 2>/dev/null
        wait $SOCAT_PID 2>/dev/null
        rm -f /tmp/fmrb_uart_core /tmp/fmrb_uart_host /tmp/socat.log
    fi
}
trap cleanup EXIT INT TERM

# Display connection info
echo ""
echo "========================================="
echo "FMRuby Core Linux - Ready"
echo "========================================="
echo "FS Proxy UART:"
echo "  Core device: ${UART_CORE}"
echo "  Host device: ${UART_HOST} (or /tmp/fmrb_uart_host)"
echo "  Use: tool/fmrb_transfer.rb --port /tmp/fmrb_uart_host shell"
echo "========================================="
echo ""

# Run FMRuby Core in Docker
echo "Starting FMRuby Core..."
if [ "$1" = "gdb" ]; then
    docker run -it --rm --user $(id -u):$(id -g) \
        -v $PWD:/project \
        -v /tmp:/tmp \
        -v /dev:/dev \
        -e FMRB_FS_PROXY_UART=${UART_CORE} \
        esp32_build_container:v5.5.1 \
        bash
else
    docker run --rm --user $(id -u):$(id -g) \
        -v $PWD:/project \
        -v /tmp:/tmp \
        -v /dev:/dev \
        -e FMRB_FS_PROXY_UART=${UART_CORE} \
        esp32_build_container:v5.5.1 \
        /project/build/fmruby-core.elf
fi

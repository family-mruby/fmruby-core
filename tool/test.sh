#!/bin/bash
# Test script for fmrb_transfer using socat virtual serial ports

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== FMRuby Transfer Test Script ===${NC}"
echo ""

# Create socat virtual serial ports
echo -e "${GREEN}Creating virtual serial ports with socat...${NC}"
socat -d -d pty,raw,echo=0,link=/tmp/fmrb_server pty,raw,echo=0,link=/tmp/fmrb_client 2>&1 | tee /tmp/socat.log &
SOCAT_PID=$!

# Wait for socat to create the devices
sleep 2

# Extract the actual PTY devices from socat output
SERVER_PORT=$(grep "N PTY is" /tmp/socat.log | head -1 | awk '{print $NF}')
CLIENT_PORT=$(grep "N PTY is" /tmp/socat.log | tail -1 | awk '{print $NF}')

# If links were created, use those instead
if [ -e /tmp/fmrb_server ]; then
    SERVER_PORT=/tmp/fmrb_server
fi
if [ -e /tmp/fmrb_client ]; then
    CLIENT_PORT=/tmp/fmrb_client
fi

echo -e "${GREEN}Server port: ${SERVER_PORT}${NC}"
echo -e "${GREEN}Client port: ${CLIENT_PORT}${NC}"
echo ""

# Create test directory
TEST_ROOT="/tmp/fmrb_test"
rm -rf "$TEST_ROOT"
mkdir -p "$TEST_ROOT"
echo "Test file content" > "$TEST_ROOT/test.txt"
mkdir -p "$TEST_ROOT/subdir"
echo "Another test file" > "$TEST_ROOT/subdir/file2.txt"

echo -e "${GREEN}Test directory created at: ${TEST_ROOT}${NC}"
echo ""

# Start the test server
echo -e "${BLUE}Starting test server...${NC}"
ruby "$(dirname "$0")/fmrb_test_server.rb" "$SERVER_PORT" "$TEST_ROOT" &
SERVER_PID=$!

# Wait for server to start
sleep 1

# Function to cleanup on exit
cleanup() {
    echo ""
    echo -e "${BLUE}Cleaning up...${NC}"
    kill $SERVER_PID 2>/dev/null || true
    kill $SOCAT_PID 2>/dev/null || true
    rm -f /tmp/socat.log
    rm -f /tmp/fmrb_server /tmp/fmrb_client
    echo -e "${GREEN}Cleanup complete${NC}"
}

trap cleanup EXIT INT TERM

echo -e "${GREEN}Server started (PID: ${SERVER_PID})${NC}"
echo ""
echo -e "${BLUE}=== Ready for testing ===${NC}"
echo ""
echo "You can now run commands like:"
echo "  ruby $(dirname "$0")/fmrb_transfer.rb --port $CLIENT_PORT shell"
echo ""
echo "Or run one-shot commands:"
echo "  ruby $(dirname "$0")/fmrb_transfer.rb --port $CLIENT_PORT remote ls /"
echo ""
echo "Press Ctrl-C to stop the test server and cleanup"
echo ""

# Keep the script running
wait $SERVER_PID

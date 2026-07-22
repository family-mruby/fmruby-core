#!/bin/bash
# Phase E1 autonomous test: on-device debugger (FMRB::Debug) + E0 exclusion.
#
# Boots the headless stack, spawns the dbg_sample target and the dbg_e1_test
# app (which drives acquire/attach/bp/stop/stack/vars/step/continue/detach
# entirely on-device), then verifies:
#   1. the test app logs "E1 TEST: PASS"
#   2. while it holds the local session, a remote attach is refused (E0).
#
# Requires: headless stack buildable (docker), python3, the root verification
# tools (dev_run_check.sh / fmrb_screenshot.py). rake build:linux must be done.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"      # fmruby-core/tool/debug
CORE="$(cd "$HERE/../.." && pwd)"          # fmruby-core
ROOT="$(cd "$CORE/.." && pwd)"             # family-mruby

PORT=5555
SAMPLE_PATH=/app/debug/dbg_sample.app.rb
SAMPLE_NAME="Debug Sample"
TEST_PATH=/app/test/dbg_e1_test.app.rb
SHOT="${1:-/tmp/fmrb_test_e1.png}"

cd "$ROOT"
CLIENT="python3 $CORE/tool/debug/fmrb_dbg_client.py localhost:$PORT"

echo "== booting headless stack =="
tools/dev_run_check.sh --keep /tmp/fmrb_e1_boot.png >/dev/null 2>&1 || {
    echo "boot failed"; exit 1; }

# Spawn the debug target first (non-hook command, always allowed).
echo "== spawning dbg_sample =="
$CLIENT spawn path=$SAMPLE_PATH >/dev/null

PID=""
for _ in $(seq 20); do
    sleep 1
    PID=$($CLIENT ps | python3 -c "import sys,ast; d=ast.literal_eval(sys.stdin.read()); \
print(next((a['pid'] for a in d['apps'] if a['name']=='$SAMPLE_NAME'), ''))")
    [ -n "$PID" ] && break
done
if [ -z "$PID" ]; then
    echo "dbg_sample did not launch; aborting"
    docker compose down >/dev/null 2>&1 || true
    exit 1
fi
echo "dbg_sample pid=$PID"

# Launch the on-device debugger test. It acquires the local session and runs
# the whole cycle by itself.
echo "== launching dbg_e1_test =="
$CLIENT spawn path=$TEST_PATH >/dev/null

echo "== waiting for the on-device test to finish =="
sleep 6
python3 "$CORE/tool/debug/fmrb_screenshot.py" "$SHOT" >/dev/null 2>&1 || true

RC=0

echo "== checking log for PASS =="
if docker compose logs 2>/dev/null | grep -q "E1 TEST: PASS"; then
    echo "PASS marker found"
else
    echo "PASS marker NOT found"
    docker compose logs 2>/dev/null | grep "E1 TEST" || true
    RC=1
fi

echo "== E0 exclusion: remote attach must be refused (BUSY) =="
# dbg_e1_test still owns the local session, so this hook command must fail.
if $CLIENT attach pid=$PID 2>&1 | grep -qi "error"; then
    echo "remote attach refused (expected)"
else
    echo "remote attach was NOT refused (E0 exclusion failed)"
    RC=1
fi

echo "== tearing down =="
docker compose down >/dev/null 2>&1 || true
echo "screenshot: $SHOT"
exit $RC

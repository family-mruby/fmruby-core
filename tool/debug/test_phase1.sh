#!/bin/bash
# Phase 1 autonomous smoke test for the remote debugger.
#
# Boots the headless stack, launches the Kamon demo (an on-device compiled
# mruby app), then drives the full debug flow (attach/bp/stop/stack_trace/
# frame_vars/step/continue/detach) via test_phase1.py. Tears the stack down.
#
# Requires: docker compose stack buildable, python3 + msgpack, the root
# verification tools (dev_run_check.sh / fmrb_input.py).
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"      # fmruby-core/tool/debug
CORE="$(cd "$HERE/../.." && pwd)"          # fmruby-core
ROOT="$(cd "$CORE/.." && pwd)"             # family-mruby

PORT=5555
FILE=/app/demo/kamon.app.rb
LINE=53

cd "$ROOT"

echo "== booting headless stack =="
tools/dev_run_check.sh --keep /tmp/fmrb_test_phase1.png >/dev/null 2>&1 || {
    echo "boot failed"; exit 1; }

# Launch through the debugger's own spawn command rather than clicking the
# launcher: icon positions shift whenever an app is added to flash/app.
echo "== launching Kamon =="
python3 "$CORE/tool/debug/fmrb_dbg_client.py" localhost:$PORT spawn path=$FILE >/dev/null

PID=""
for _ in $(seq 20); do
    sleep 1
    PID=$(python3 "$CORE/tool/debug/fmrb_dbg_client.py" localhost:$PORT ps \
        | python3 -c "import sys,ast; d=ast.literal_eval(sys.stdin.read()); \
print(next((a['pid'] for a in d['apps'] if a['name']=='Kamon'), ''))")
    [ -n "$PID" ] && break
done
if [ -z "$PID" ]; then
    echo "Kamon did not launch; aborting"
    docker compose down >/dev/null 2>&1 || true
    exit 1
fi
echo "Kamon pid=$PID"

echo "== running debug flow =="
set +e
python3 "$CORE/tool/debug/test_phase1.py" localhost:$PORT "$PID" "$FILE" "$LINE"
RC=$?
set -e

echo "== tearing down =="
docker compose down >/dev/null 2>&1 || true
exit $RC

#!/bin/bash
# Phase 2 autonomous test: combined line-map unit check + DAP adapter E2E.
#
# Boots the headless stack, launches Kamon, then drives fmrb_dap_adapter.py
# over DAP (as VSCode would) via test_phase2.py. Tears the stack down.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/../.." && pwd)"
ROOT="$(cd "$CORE/.." && pwd)"

PORT=5555
APP=Kamon
SRC=kamon.app.rb
PATH_RB=/app/demo/kamon.app.rb
LINE=53

cd "$ROOT"

echo "== booting headless stack =="
tools/dev_run_check.sh --keep /tmp/fmrb_test_phase2.png >/dev/null 2>&1 || {
    echo "boot failed"; exit 1; }

# Launch through the debugger's own spawn command rather than clicking the
# launcher: icon positions shift whenever an app is added to flash/app.
echo "== launching Kamon =="
python3 "$CORE/tool/debug/fmrb_dbg_client.py" localhost:$PORT spawn path=$PATH_RB >/dev/null
for _ in $(seq 20); do
    sleep 1
    python3 "$CORE/tool/debug/fmrb_dbg_client.py" localhost:$PORT ps \
        | grep -q "'$APP'" && break
done

echo "== running DAP test =="
set +e
python3 "$CORE/tool/debug/test_phase2.py" localhost:$PORT "$APP" "$SRC" "$LINE"
RC=$?
set -e

echo "== tearing down =="
docker compose down >/dev/null 2>&1 || true
exit $RC

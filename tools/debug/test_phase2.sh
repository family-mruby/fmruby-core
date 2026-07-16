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
LINE=53
KAMON_XY="270 120"

cd "$ROOT"

echo "== booting headless stack =="
tools/dev_run_check.sh --keep /tmp/fmrb_test_phase2.png >/dev/null 2>&1 || {
    echo "boot failed"; exit 1; }

echo "== launching Kamon =="
python3 tools/fmrb_input.py \
    click 20 5 sleep 700 click 20 17 sleep 2500 \
    click $KAMON_XY sleep 150 click $KAMON_XY sleep 2500 >/dev/null

echo "== running DAP test =="
set +e
python3 "$CORE/tools/debug/test_phase2.py" localhost:$PORT "$APP" "$SRC" "$LINE"
RC=$?
set -e

echo "== tearing down =="
docker compose down >/dev/null 2>&1 || true
exit $RC

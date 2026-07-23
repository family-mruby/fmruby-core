#!/usr/bin/env bash
# Phase 0 PoC runner: build-free verification that the harness, msgpack subset,
# and coverage scripts all produce byte-identical output on CRuby and Spinel.
#
# Prereq: Spinel built at tmp/spinel/bin/spinel (make deps && make in the fork).
#
# Usage: tool/spinel_poc/run_all.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SP="${SPINEL:-$HERE/../../../tmp/spinel/bin/spinel}"
if [ ! -x "$SP" ]; then echo "spinel not found at $SP (build the fork first)"; exit 2; fi

pass=0; fail=0
check() { # name, cruby_cmd, spinel_cmd
  local name="$1"; shift
  ruby $1 > /tmp/pa_cruby 2>/dev/null
  "$SP" -E $2 > /tmp/pa_spinel 2>/dev/null
  if [ $? -ne 0 ]; then echo "FAIL $name (spinel run error)"; fail=$((fail+1)); return; fi
  if diff -q /tmp/pa_cruby /tmp/pa_spinel >/dev/null; then
    echo "ok   $name"; pass=$((pass+1))
  else
    echo "DIFF $name"; diff /tmp/pa_cruby /tmp/pa_spinel | head -8; fail=$((fail+1))
  fi
}

cd "$HERE"
check "harness_input_router" "harness_input_router.rb" "harness_input_router.rb"
check "test_msgpack" "test_msgpack.rb" "test_msgpack.rb"
for f in coverage/cov_*.rb; do
  check "$(basename "$f")" "$f" "$f"
done

echo "----"
echo "PoC parity: $pass ok, $fail fail"
[ "$fail" -eq 0 ]

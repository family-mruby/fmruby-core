#!/bin/bash
# Run every golden case against the host BASIC runner and compare the output.
#
# A case is a pair test/golden/NNN_name.bas + NNN_name.expected; the runner
# output (stdout) must match .expected byte for byte. An optional
# NNN_name.input file is passed to the runner as the INPUT source.
#
# Usage: run_golden.sh [runner-binary] [case-filter]
#   runner-binary  default: $BASIC_RUNNER or test/build/basic_runner
#   case-filter    substring; only matching case names run
set -u

here="$(cd "$(dirname "$0")" && pwd)"
runner="${1:-${BASIC_RUNNER:-$here/build/basic_runner}}"
filter="${2:-}"

if [ ! -x "$runner" ]; then
  echo "runner not found: $runner (build it with 'rake basic:test')" >&2
  exit 2
fi

pass=0
fail=0
failed_cases=""

for bas in "$here"/golden/*.bas; do
  [ -e "$bas" ] || continue
  name="$(basename "$bas" .bas)"
  case "$name" in
    *"$filter"*) ;;
    *) continue ;;
  esac

  expected="$here/golden/$name.expected"
  if [ ! -f "$expected" ]; then
    echo "FAIL $name (no .expected file)"
    fail=$((fail + 1))
    failed_cases="$failed_cases $name"
    continue
  fi

  input="$here/golden/$name.input"
  actual="$(mktemp)"
  if [ -f "$input" ]; then
    "$runner" "$bas" "$input" >"$actual" 2>/dev/null
  else
    "$runner" "$bas" >"$actual" 2>/dev/null </dev/null
  fi

  # Byte for byte comparison: trailing newlines are part of the expectation.
  if cmp -s "$expected" "$actual"; then
    echo "PASS $name"
    pass=$((pass + 1))
  else
    echo "FAIL $name"
    diff -u "$expected" "$actual" | sed 's/^/    /'
    fail=$((fail + 1))
    failed_cases="$failed_cases $name"
  fi
  rm -f "$actual"
done

echo "---"
echo "golden: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
  echo "failed:$failed_cases"
  exit 1
fi
exit 0

#!/usr/bin/env python3
"""Cross-check the Python BLE frame codec against the device implementation.

Reads "<bodyhex> <framehex>" lines (as emitted by `framing_vec fuzz N`, which
links the real ble_framing.c) from stdin and asserts that:
  - encode_frame(body) reproduces the device frame byte for byte, and
  - decode_frame(device frame) recovers the body.

Usage (from tool/debug/, after building framing_vec per its header comment):
    ./framing_vec fuzz 2000 | python3 test_ble_framing_cross.py
"""
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fmrb_ble_framing import decode_frame, encode_frame  # noqa: E402


def main():
    checked = 0
    for lineno, line in enumerate(sys.stdin, 1):
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            print(f"line {lineno}: malformed input", file=sys.stderr)
            return 2
        body = bytes.fromhex(parts[0]) if parts[0] else b""
        frame = bytes.fromhex(parts[1])
        if encode_frame(body) != frame:
            print(f"line {lineno}: encode mismatch (body {len(body)} bytes)",
                  file=sys.stderr)
            return 1
        if decode_frame(frame) != body:
            print(f"line {lineno}: decode mismatch (body {len(body)} bytes)",
                  file=sys.stderr)
            return 1
        checked += 1
    if checked == 0:
        print("no vectors on stdin (pipe `framing_vec fuzz N` into this)",
              file=sys.stderr)
        return 2
    print(f"RESULT: PASS ({checked} frames match the device codec)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

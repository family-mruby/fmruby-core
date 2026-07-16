#!/usr/bin/env python3
"""Concatenate Ruby source files into a *_combined.rb and emit a line map.

Used by main/prebuild_scripts/compile_ruby_to_bytecode.cmake in place of a
plain `cat`, so the remote debugger can map a combined-file line number back to
the original source file:line (the device only ever sees the combined line
number). The map is consumed by tool/debug/fmrb_dap_adapter.py.

Usage:
  gen_combined_rb.py <combined_out.rb> <map_out.json> <in1.rb> [in2.rb ...]

Byte-for-byte identical concatenation to `cat in1 in2 ...`; the map records,
per input file, its 1-based start line in the combined output and its line
count.
"""
import json
import sys


def main():
    if len(sys.argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    combined_out, map_out = sys.argv[1], sys.argv[2]
    inputs = sys.argv[3:]

    segments = []
    cur_line = 1          # 1-based line where the next file begins
    with open(combined_out, "wb") as out:
        for path in inputs:
            with open(path, "rb") as f:
                data = f.read()
            out.write(data)
            nl = data.count(b"\n")
            # Line count contributed: newline count, +1 if the final line has
            # content without a trailing newline (cat would merge it with the
            # next file's first line, but that line still "belongs" here).
            lines = nl + (1 if data and not data.endswith(b"\n") else 0)
            segments.append({
                "file": path,
                "start_line": cur_line,
                "lines": lines,
            })
            cur_line += nl   # next file starts after the last newline

    with open(map_out, "w") as f:
        json.dump(segments, f, indent=1)

    return 0


if __name__ == "__main__":
    sys.exit(main())

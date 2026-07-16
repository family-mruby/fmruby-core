#!/usr/bin/env python3
"""Phase 2 autonomous test: exercises fmrb_dap_adapter.py like VSCode would.

Part A (no device): unit-check the combined-file line Mapper against the real
generated *_combined.map.json.

Part B (device): spawn the DAP adapter as a subprocess, speak DAP over its
stdio, and drive attach -> setBreakpoints -> stopped -> stackTrace -> scopes ->
variables -> next -> continue -> disconnect against a running app.

Usage: test_phase2.py <host:port> <app-name> <source-basename> <line>
The stack must be up with <app-name> running.
"""
import json
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from fmrb_dap_adapter import Mapper  # noqa: E402

fails = []


def check(cond, msg):
    print(("  OK   " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


# --------------------------------------------------------------------------
def part_a_mapper():
    print("== Part A: combined line mapper ==")
    maps_glob = os.path.join(CORE, "main", "prebuild_scripts", "*", "mrb")
    m = Mapper(
        path_mappings=[{"device": "/app/", "local": os.path.join(CORE, "flash/app/")}],
        project_mappings=[{"device": "/project/", "local": CORE + "/"}],
        combined_map_globs=[maps_glob],
    )
    check("system_desktop_combined.rb" in m.combined,
          "loaded system_desktop combined map")
    if "system_desktop_combined.rb" not in m.combined:
        return
    # Pick the 2nd segment (offset != 0) for a meaningful round trip.
    segs = m.combined["system_desktop_combined.rb"]
    seg = segs[1] if len(segs) > 1 else segs[0]
    base = seg["base"]
    dev_file, dev_line = m.to_device(base, 5)
    check(dev_file == "system_desktop_combined.rb",
          f"to_device({base}:5) file = {dev_file}")
    check(dev_line == seg["start"] + 4,
          f"to_device({base}:5) line = {dev_line} (expect {seg['start']+4})")
    local, oline = m.to_source("system_desktop_combined.rb", dev_line)
    check(oline == 5, f"round-trip line back to 5 (got {oline})")
    check(os.path.basename(local) == base, f"round-trip file back to {base}")
    # Standalone file passes through by basename.
    df, dl = m.to_device("/whatever/kamon.app.rb", 53)
    check(df == "kamon.app.rb" and dl == 53, "standalone file maps by basename")


# --------------------------------------------------------------------------
class DapClient:
    def __init__(self, proc, on_event):
        self.proc = proc
        self.on_event = on_event
        self.seq = 0
        self._pending = {}
        self._lock = threading.Lock()
        self._t = threading.Thread(target=self._reader, daemon=True)
        self._t.start()

    def _reader(self):
        buf = b""
        f = self.proc.stdout
        while True:
            # read headers
            headers = {}
            line = f.readline()
            if not line:
                return
            while line and line.strip():
                if b":" in line:
                    k, v = line.split(b":", 1)
                    headers[k.strip().lower()] = v.strip()
                line = f.readline()
            n = int(headers.get(b"content-length", b"0"))
            body = f.read(n)
            msg = json.loads(body.decode())
            if msg.get("type") == "response":
                with self._lock:
                    ev = self._pending.pop(msg["request_seq"], None)
                if ev:
                    ev[1].append(msg)
                    ev[0].set()
            elif msg.get("type") == "event":
                self.on_event(msg.get("event"), msg.get("body") or {})

    def request(self, command, arguments=None, timeout=8.0):
        with self._lock:
            self.seq += 1
            seq = self.seq
            ev = threading.Event()
            holder = []
            self._pending[seq] = (ev, holder)
        msg = {"seq": seq, "type": "request", "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        data = json.dumps(msg).encode()
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data))
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        if not ev.wait(timeout):
            raise TimeoutError(f"no response to {command}")
        return holder[0]


def part_b_e2e(host, port, app, src_base, line):
    print("== Part B: DAP adapter end-to-end ==")
    proc = subprocess.Popen(
        [sys.executable, os.path.join(HERE, "fmrb_dap_adapter.py")],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    stopped = threading.Event()
    events = []

    def on_event(name, body):
        events.append((name, body))
        print(f"  [dap-event] {name}: {body}")
        if name == "stopped":
            stopped.set()

    dap = DapClient(proc, on_event)
    try:
        r = dap.request("initialize", {"adapterID": "fmrb"})
        check(r["success"], "initialize ok")

        r = dap.request("attach", {
            "host": host, "port": port, "app": app,
            "pathMappings": [{"device": "/app/", "local": os.path.join(CORE, "flash/app/")}],
            "projectMappings": [{"device": "/project/", "local": CORE + "/"}],
            "combinedMaps": [os.path.join(CORE, "main", "prebuild_scripts", "*", "mrb")],
        })
        check(r["success"], f"attach to {app} ok")

        r = dap.request("setBreakpoints", {
            "source": {"path": f"/host/{src_base}"},
            "breakpoints": [{"line": line}],
        })
        check(r["success"] and r["body"]["breakpoints"][0]["verified"],
              f"breakpoint {src_base}:{line} verified")

        dap.request("configurationDone")

        check(stopped.wait(8.0), "received DAP stopped event")
        if stopped.is_set():
            r = dap.request("threads")
            check(len(r["body"]["threads"]) >= 1, "threads returned")
            tid = r["body"]["threads"][0]["id"]

            r = dap.request("stackTrace", {"threadId": tid})
            frames = r["body"]["stackFrames"]
            for fr in frames:
                sp = fr.get("source") or {}
                print(f"    #{fr['id']} {fr['name']} {sp.get('path','')}:{fr['line']}")
            check(len(frames) >= 1, "stackTrace returned frames")
            check(frames[0]["line"] == line,
                  f"top frame at line {line} (got {frames[0]['line']})")

            r = dap.request("scopes", {"frameId": frames[0]["id"]})
            ref = r["body"]["scopes"][0]["variablesReference"]
            r = dap.request("variables", {"variablesReference": ref})
            print(f"    variables: {r['body']['variables']}")
            check(r["success"], "variables returned")

            stopped.clear()
            dap.request("next", {"threadId": tid})
            check(stopped.wait(8.0), "stopped again after next (step over)")

            dap.request("continue", {"threadId": tid})

        r = dap.request("disconnect", {})
        check(r["success"], "disconnect ok")
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass
        time.sleep(0.3)
        proc.terminate()


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    target, app, src_base, line = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    host, port = (target.rsplit(":", 1)[0], int(target.rsplit(":", 1)[1])) \
        if ":" in target else (target, 5555)

    part_a_mapper()
    part_b_e2e(host, port, app, src_base, line)

    print()
    if fails:
        print(f"RESULT: FAIL ({len(fails)})")
        for m in fails:
            print("  -", m)
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

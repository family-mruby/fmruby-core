#!/usr/bin/env python3
"""Phase 1 autonomous smoke test for the remote debugger.

Assumes the stack is already up (tools/dev_run_check.sh --keep) and a debuggable
mruby app is running. Given a target pid + source file + line, it exercises the
full flow: attach -> bp_set -> wait stopped -> stack_trace -> frame_vars ->
step_over -> continue -> detach, asserting along the way.

Usage: test_phase1.py <host:port> <pid> <file> <line>
Exit code 0 on success, non-zero on failure.
"""
import sys
import threading

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fmrb_dbg_client import FmrbDebugClient, FmrbDebugError  # noqa: E402


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    target, pid, file, line = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
    host, port = (target.rsplit(":", 1)[0], int(target.rsplit(":", 1)[1])) \
        if ":" in target else (target, 5555)

    stopped = threading.Event()
    resumed = threading.Event()
    events = []

    def on_event(name, payload):
        events.append((name, payload))
        print(f"  [event] {name}: {payload}")
        if name == "stopped":
            stopped.set()
        elif name == "resumed":
            resumed.set()

    fails = []

    def check(cond, msg):
        print(("  OK   " if cond else "  FAIL ") + msg)
        if not cond:
            fails.append(msg)

    cli = FmrbDebugClient(host, port, on_event=on_event)
    cli.connect()
    try:
        print("== version =="); print(" ", cli.version())

        print("== ps ==")
        apps = cli.ps()
        pids = [a["pid"] for a in apps]
        check(pid in pids, f"target pid {pid} present in ps {pids}")

        print(f"== attach {pid} ==")
        cli.attach(pid)
        check(cli.request("version") is not None, "still responsive after attach")

        print(f"== bp_set {file}:{line} ==")
        bp_id = cli.bp_set(pid, file, line)
        check(isinstance(bp_id, int) and bp_id >= 1, f"bp_id assigned = {bp_id}")

        print("== wait for stopped (<=8s) ==")
        got = stopped.wait(8.0)
        check(got, "received a stopped event")
        if got:
            name, payload = next(e for e in events if e[0] == "stopped")
            check(payload.get("reason") == "breakpoint", "stop reason is breakpoint")
            check(payload.get("line") == line, f"stopped at line {line} (got {payload.get('line')})")

            print("== stack_trace ==")
            frames = cli.stack_trace(pid)
            for f in frames:
                print(f"    #{f['idx']} {f['func']} at {f['file']}:{f['line']}")
            check(len(frames) >= 1, f"stack_trace returned {len(frames)} frame(s)")

            print("== frame_vars 0 ==")
            vs = cli.frame_vars(pid, 0)
            for v in vs:
                print(f"    {v['name']}: {v['value']} ({v['type']}) ref={v.get('ref', 0)}")
            check(True, "frame_vars returned without error")
            check(all("ref" in v for v in vs), "frame_vars entries carry a ref field")

            # Find an expandable local across all frames (e.g. the top-level
            # `app` object) and expand it, then expand a nested container child.
            expandable = None
            for f in frames:
                fv = cli.frame_vars(pid, f["idx"])
                expandable = next((v for v in fv if v.get("ref", 0) > 0), None)
                if expandable:
                    break
            check(expandable is not None, "found an expandable value to inspect")
            if expandable:
                print(f"== expand {expandable['name']} (ref={expandable['ref']}) ==")
                children = cli.expand(pid, expandable["ref"])
                for c in children:
                    print(f"    {c['name']}: {c['value']} ({c['type']}) ref={c.get('ref', 0)}")
                check(isinstance(children, list) and len(children) > 0,
                      "expand returned children")
                check(all("ref" in c for c in children),
                      "expand children carry a ref field")

                nested = next((c for c in children if c.get("ref", 0) > 0), None)
                if nested:
                    print(f"== expand nested {nested['name']} (ref={nested['ref']}) ==")
                    grand = cli.expand(pid, nested["ref"])
                    for c in grand:
                        print(f"    {c['name']}: {c['value']} ({c['type']}) ref={c.get('ref', 0)}")
                    check(isinstance(grand, list), "nested expand returned a list")

            try:
                cli.expand(pid, 999999)
                bad_ok = False
            except FmrbDebugError:
                bad_ok = True
            check(bad_ok, "expand rejects an invalid handle")

            print("== step_over ==")
            resumed.clear(); stopped.clear()
            cli.step(pid, "over")
            check(stopped.wait(8.0), "stopped again after step_over")

            print("== continue ==")
            resumed.clear()
            cli.cont(pid)
            check(resumed.wait(4.0), "received resumed after continue")

        print(f"== detach {pid} ==")
        cli.detach(pid)
        check(not cli.request("version") is None, "responsive after detach")

    except (FmrbDebugError, TimeoutError) as e:
        fails.append(f"exception: {e}")
        print(f"  EXC  {e}")
    finally:
        cli.close()

    print()
    if fails:
        print(f"RESULT: FAIL ({len(fails)} check(s) failed)")
        for m in fails:
            print("  -", m)
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

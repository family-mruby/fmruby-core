#!/usr/bin/env python3
"""Test client / library for the PicoRuby remote debugger (fmrb_debugd).

Speaks the msgpack protocol in doc/vm_remote_debug_protocol.md over TCP.
Doubles as:
  - a library (FmrbDebugClient) used by fmrb_dap_adapter.py (Phase 2), and
  - a small interactive/one-shot CLI for manual testing.

Requires the `msgpack` package.

CLI:
  python3 fmrb_dbg_client.py <host:port> <cmd> [k=v ...]   # one-shot
  python3 fmrb_dbg_client.py <host:port>                   # interactive REPL
"""
import argparse
import socket
import struct
import sys
import threading

try:
    import msgpack
except ImportError:
    sys.stderr.write("error: the 'msgpack' package is required (pip install msgpack)\n")
    raise

# Message type tags (first array element).
MSG_REQUEST = 0
MSG_RESPONSE = 1
MSG_EVENT = 2


class FmrbDebugError(Exception):
    def __init__(self, err, cmd):
        super().__init__(f"command {cmd!r} failed with err={err}")
        self.err = err
        self.cmd = cmd


class FmrbDebugClient:
    """Synchronous request/response client with a background event reader.

    Events (stopped/resumed/exited/output) are delivered to an optional
    on_event(name, payload) callback from the reader thread.
    """

    def __init__(self, host, port, on_event=None, timeout=5.0):
        self.host = host
        self.port = port
        self.on_event = on_event
        self.timeout = timeout
        self._sock = None
        self._seq = 0
        self._lock = threading.Lock()
        self._pending = {}          # seq -> (Event, [result_holder])
        self._reader = None
        self._running = False
        self._unpacker = msgpack.Unpacker(raw=False)

    # --- connection -------------------------------------------------------
    def connect(self):
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._sock.settimeout(None)
        self._running = True
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def close(self):
        self._running = False
        if self._sock:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._sock.close()
            self._sock = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *a):
        self.close()

    # --- framing ----------------------------------------------------------
    def _send_frame(self, body):
        self._sock.sendall(struct.pack(">I", len(body)) + body)

    def _read_loop(self):
        try:
            while self._running:
                chunk = self._sock.recv(65536)
                if not chunk:
                    break
                # Our frames are length-prefixed, but msgpack bodies are
                # self-delimiting, so we can feed the concatenated bodies
                # (minus the 4-byte prefixes) to a streaming Unpacker. Strip
                # prefixes manually to stay strict about frame boundaries.
                self._feed(chunk)
        except OSError:
            pass
        finally:
            self._running = False

    def _feed(self, chunk):
        # Reassemble length-prefixed frames.
        self._rxbuf = getattr(self, "_rxbuf", b"") + chunk
        while len(self._rxbuf) >= 4:
            (length,) = struct.unpack(">I", self._rxbuf[:4])
            if len(self._rxbuf) < 4 + length:
                break
            body = self._rxbuf[4:4 + length]
            self._rxbuf = self._rxbuf[4 + length:]
            self._dispatch(msgpack.unpackb(body, raw=False))

    def _dispatch(self, msg):
        if not isinstance(msg, list) or len(msg) < 3:
            return
        mtype = msg[0]
        if mtype == MSG_RESPONSE:
            _, seq, err = msg[0], msg[1], msg[2]
            payload = msg[3] if len(msg) > 3 else None
            with self._lock:
                waiter = self._pending.pop(seq, None)
            if waiter:
                ev, holder = waiter
                holder.append((err, payload))
                ev.set()
        elif mtype == MSG_EVENT:
            name = msg[2]
            payload = msg[3] if len(msg) > 3 else {}
            if self.on_event:
                self.on_event(name, payload)

    # --- requests ---------------------------------------------------------
    def request(self, cmd, payload=None, timeout=None):
        with self._lock:
            self._seq = (self._seq + 1) & 0xFFFF
            seq = self._seq
            ev = threading.Event()
            holder = []
            self._pending[seq] = (ev, holder)
        body = msgpack.packb([MSG_REQUEST, seq, cmd, payload], use_bin_type=True)
        self._send_frame(body)
        if not ev.wait(timeout if timeout is not None else self.timeout):
            with self._lock:
                self._pending.pop(seq, None)
            raise TimeoutError(f"no response to {cmd!r} (seq={seq})")
        err, resp_payload = holder[0]
        if err != 0:
            raise FmrbDebugError(err, cmd)
        return resp_payload

    # --- typed helpers ----------------------------------------------------
    def version(self):
        return self.request("version")

    def ps(self):
        return self.request("ps")["apps"]

    def attach(self, pid):
        return self.request("attach", {"pid": pid})

    def detach(self, pid):
        return self.request("detach", {"pid": pid})

    def bp_set(self, pid, file, line):
        return self.request("bp_set", {"pid": pid, "file": file, "line": line})["bp_id"]

    def bp_clear(self, pid, bp_id=-1):
        return self.request("bp_clear", {"pid": pid, "bp_id": bp_id})

    def pause(self, pid):
        return self.request("pause", {"pid": pid})

    def cont(self, pid):
        return self.request("continue", {"pid": pid})

    def step(self, pid, kind="over"):
        return self.request("step_" + kind, {"pid": pid})

    def stack_trace(self, pid, max_frames=32):
        return self.request("stack_trace", {"pid": pid, "max": max_frames})["frames"]

    def frame_vars(self, pid, frame=0):
        return self.request("frame_vars", {"pid": pid, "frame": frame})["vars"]

    def log_read(self, pos=0, max_lines=50):
        return self.request("log_read", {"pos": pos, "max_lines": max_lines})

    def kill(self, pid):
        return self.request("kill", {"pid": pid})

    def spawn(self, path):
        return self.request("spawn", {"path": path})["pid"]


# --- CLI ------------------------------------------------------------------
def _parse_hostport(s):
    if ":" in s:
        host, port = s.rsplit(":", 1)
        return host, int(port)
    return s, 5555


def _coerce(v):
    try:
        return int(v)
    except ValueError:
        return v


def _print_event(name, payload):
    print(f"\n[event] {name}: {payload}")


def cmd_oneshot(client, cmd, kv):
    payload = {k: _coerce(v) for k, v in (a.split("=", 1) for a in kv)} or None
    try:
        resp = client.request(cmd, payload)
        print(resp)
        return 0
    except FmrbDebugError as e:
        print(f"error: {e}")
        return 1


def repl(client):
    print("connected. commands: ps | attach PID | detach PID | b FILE:LINE (pid via 'attach') |")
    print("  c | bt | vars [FRAME] | pause | si|so|sv (step in/out/over) | log | q")
    cur_pid = None
    while True:
        try:
            line = input("fdbg> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        parts = line.split()
        op = parts[0]
        try:
            if op in ("q", "quit", "exit"):
                break
            elif op == "version":
                print(client.version())
            elif op == "ps":
                for a in client.ps():
                    print(f"  pid={a['pid']:<3} {a['name']:<18} state={a['state']} "
                          f"vm={a['vm']} mem={a['mem_used']}/{a['mem_total']}")
            elif op == "attach":
                cur_pid = int(parts[1])
                print(client.attach(cur_pid))
            elif op == "detach":
                pid = int(parts[1]) if len(parts) > 1 else cur_pid
                print(client.detach(pid))
            elif op == "b":
                file, ln = parts[1].rsplit(":", 1)
                print("bp_id =", client.bp_set(cur_pid, file, int(ln)))
            elif op == "c":
                print(client.cont(cur_pid))
            elif op == "pause":
                print(client.pause(cur_pid))
            elif op in ("si", "so", "sv"):
                kind = {"si": "in", "so": "out", "sv": "over"}[op]
                print(client.step(cur_pid, kind))
            elif op == "bt":
                for f in client.stack_trace(cur_pid):
                    print(f"  #{f['idx']} {f['func']} at {f['file']}:{f['line']}")
            elif op == "vars":
                frame = int(parts[1]) if len(parts) > 1 else 0
                for v in client.frame_vars(cur_pid, frame):
                    print(f"  {v['name']}: {v['value']} ({v['type']})")
            elif op == "log":
                r = client.log_read()
                print(r["lines"])
            else:
                print(f"unknown: {op}")
        except (FmrbDebugError, TimeoutError, IndexError, ValueError) as e:
            print(f"error: {e}")


def main():
    ap = argparse.ArgumentParser(description="fmrb remote debugger test client")
    ap.add_argument("target", help="host:port (default port 5555)")
    ap.add_argument("cmd", nargs="?", help="one-shot command (omit for REPL)")
    ap.add_argument("args", nargs="*", help="k=v payload pairs")
    args = ap.parse_args()

    host, port = _parse_hostport(args.target)
    client = FmrbDebugClient(host, port, on_event=_print_event)
    client.connect()
    try:
        if args.cmd:
            sys.exit(cmd_oneshot(client, args.cmd, args.args))
        else:
            repl(client)
    finally:
        client.close()


if __name__ == "__main__":
    main()

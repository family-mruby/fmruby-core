#!/usr/bin/env python3
"""Test client / library for the PicoRuby remote debugger (fmrb_debugd).

Speaks the msgpack protocol in doc/vm_remote_debug_protocol.md. The protocol
itself is transport independent; framing and I/O live in a transport object:

  - TcpTransport (here)          - Linux simulation, u32 BE length prefix
  - BleTransport (fmrb_ble_transport) - ESP32 device, COBS + CRC32 over GATT

Doubles as:
  - a library (FmrbDebugClient) used by fmrb_dap_adapter.py (Phase 2), and
  - a small interactive/one-shot CLI for manual testing.

Requires the `msgpack` package; BLE targets additionally require `bleak`,
which is imported only when a BLE target is used.

CLI:
  python3 fmrb_dbg_client.py <target> <cmd> [k=v ...]   # one-shot
  python3 fmrb_dbg_client.py <target>                   # interactive REPL

  <target> is host[:port] for TCP (default port 5555), or "ble" to scan for a
  single Family-mruby device, or "ble:<name-or-address>" to pick one.
"""
import argparse
import json
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


class TcpTransport:
    """u32 BE length-prefixed msgpack bodies over a TCP socket.

    A background reader thread reassembles frames and hands each complete body
    to the handler registered with set_body_handler().
    """

    DEFAULT_PORT = 5555

    def __init__(self, host, port=DEFAULT_PORT, timeout=5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock = None
        self._reader = None
        self._running = False
        self._rxbuf = b""
        self._on_body = None

    def __str__(self):
        return f"tcp {self.host}:{self.port}"

    def set_body_handler(self, cb):
        self._on_body = cb

    def connect(self):
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._sock.settimeout(None)
        self._running = True
        self._rxbuf = b""
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

    def send_body(self, body):
        if not self._sock:
            raise ConnectionError("not connected")
        self._sock.sendall(struct.pack(">I", len(body)) + body)

    def _read_loop(self):
        try:
            while self._running:
                chunk = self._sock.recv(65536)
                if not chunk:
                    break
                self._feed(chunk)
        except OSError:
            pass
        finally:
            self._running = False

    def _feed(self, chunk):
        self._rxbuf += chunk
        while len(self._rxbuf) >= 4:
            (length,) = struct.unpack(">I", self._rxbuf[:4])
            if len(self._rxbuf) < 4 + length:
                break
            body = self._rxbuf[4:4 + length]
            self._rxbuf = self._rxbuf[4 + length:]
            if self._on_body:
                self._on_body(body)


def _parse_target(target, timeout=5.0):
    """Build a transport from a target string.

    "host", "host:port"      -> TcpTransport
    "ble"                    -> BleTransport, scan for a single device
    "ble:<name-or-address>"  -> BleTransport, pick that device
    """
    if target == "ble" or target.startswith("ble:"):
        # Imported lazily so TCP users do not need bleak installed.
        try:
            from fmrb_ble_transport import BleTransport
        except ImportError as e:
            raise ImportError(
                "BLE targets need the 'bleak' package: pip install bleak "
                f"(import failed: {e})") from e
        spec = target[4:] if target.startswith("ble:") else ""
        return BleTransport(spec or None, timeout=timeout)
    if ":" in target:
        host, port = target.rsplit(":", 1)
        return TcpTransport(host, int(port), timeout=timeout)
    return TcpTransport(target, TcpTransport.DEFAULT_PORT, timeout=timeout)


class FmrbDebugClient:
    """Synchronous request/response client with a background event reader.

    Events (stopped/resumed/exited/output) are delivered to an optional
    on_event(name, payload) callback from the reader thread.
    """

    # Connection handshake: the BLE device can silently drop frames sent
    # between GATT connect and debugd registering its transport (see
    # doc/vm_remote_debug_protocol.md). Retry a cheap command until it answers.
    HANDSHAKE_TRIES = 3
    HANDSHAKE_TIMEOUT = 2.0

    def __init__(self, transport, on_event=None, timeout=5.0):
        self.transport = transport
        self.on_event = on_event
        self.timeout = timeout
        self._seq = 0
        self._lock = threading.Lock()
        self._pending = {}          # seq -> (Event, [result_holder])

    @classmethod
    def from_target(cls, target, on_event=None, timeout=5.0):
        """Build a client from a target string (see _parse_target)."""
        return cls(_parse_target(target, timeout=timeout),
                   on_event=on_event, timeout=timeout)

    # --- connection -------------------------------------------------------
    def connect(self):
        self.transport.set_body_handler(self._on_body)
        self.transport.connect()
        try:
            self._handshake()
        except Exception:
            self.transport.close()
            raise

    def _handshake(self):
        """Confirm the daemon answers before the caller sends real commands."""
        last = None
        for _ in range(self.HANDSHAKE_TRIES):
            try:
                return self.request("version", timeout=self.HANDSHAKE_TIMEOUT)
            except TimeoutError as e:
                last = e
        raise TimeoutError(
            f"no response from {self.transport} after {self.HANDSHAKE_TRIES} "
            f"attempts; is the debugger daemon running?") from last

    def close(self):
        self.transport.close()

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *a):
        self.close()

    # --- receive ----------------------------------------------------------
    def _on_body(self, body):
        try:
            msg = msgpack.unpackb(body, raw=False)
        except Exception:
            sys.stderr.write("warning: dropping undecodable message body\n")
            return
        self._dispatch(msg)

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
        try:
            self.transport.send_body(body)
        except Exception:
            with self._lock:
                self._pending.pop(seq, None)
            raise
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

    def expand(self, pid, handle):
        return self.request("expand", {"pid": pid, "handle": handle})["vars"]

    def log_read(self, pos=0, max_lines=50):
        return self.request("log_read", {"pos": pos, "max_lines": max_lines})

    def kill(self, pid):
        return self.request("kill", {"pid": pid})

    def spawn(self, path):
        return self.request("spawn", {"path": path})["pid"]


# --- CLI ------------------------------------------------------------------
def _coerce(v):
    try:
        return int(v)
    except ValueError:
        return v


def _print_event(name, payload):
    print(f"\n[event] {name}: {payload}")


def cmd_oneshot(client, cmd, kv, as_json=False):
    payload = {k: _coerce(v) for k, v in (a.split("=", 1) for a in kv)} or None
    try:
        resp = client.request(cmd, payload)
        if as_json:
            print(json.dumps(resp))
        else:
            print(resp)
        return 0
    except FmrbDebugError as e:
        if as_json:
            print(json.dumps({"error": e.err, "cmd": e.cmd}))
        else:
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
    ap.add_argument("target",
                    help="host[:port] for TCP (default port 5555), "
                         "'ble' to scan, or 'ble:<name-or-address>'")
    ap.add_argument("cmd", nargs="?", help="one-shot command (omit for REPL)")
    ap.add_argument("args", nargs="*", help="k=v payload pairs")
    ap.add_argument("--json", action="store_true",
                    help="one-shot: print the response as JSON (for tooling)")
    args = ap.parse_args()

    on_event = None if args.json else _print_event
    client = FmrbDebugClient.from_target(args.target, on_event=on_event)
    try:
        client.connect()
    except Exception as e:
        sys.stderr.write(f"error: {e}\n")
        sys.exit(1)
    try:
        if args.cmd:
            sys.exit(cmd_oneshot(client, args.cmd, args.args, as_json=args.json))
        else:
            repl(client)
    finally:
        client.close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Debug Adapter Protocol (DAP) adapter for the PicoRuby remote debugger.

Bridges VSCode (DAP over stdio) to fmrb_debugd (msgpack over TCP, via
FmrbDebugClient). One attached VM is presented as a single DAP thread.

launch.json (type "fmrb", request "attach"):
  {
    "type": "fmrb", "request": "attach",
    "host": "localhost", "port": 5555,
    "app": "Kamon",                       // app name or numeric pid
    "pathMappings": [                     // device path <-> local workspace
      { "device": "/app/", "local": "${workspaceFolder}/fmruby-core/flash/app/" }
    ],
    "projectMappings": [                  // for combined-file segment paths
      { "device": "/project/", "local": "${workspaceFolder}/fmruby-core/" }
    ],
    "combinedMaps": [                     // dirs/globs holding *_combined.map.json
      "${workspaceFolder}/fmruby-core/main/prebuild_scripts/*/mrb"
    ]
  }

Standalone-file apps (e.g. flash/app/.../foo.app.rb) need no combined map:
device breakpoint matching is by basename. Combined apps (kernel/system_*)
are translated combined-line <-> original file:line via the .map.json files.
"""
import glob
import json
import os
import struct
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fmrb_dbg_client import FmrbDebugClient, FmrbDebugError  # noqa: E402


# ============================ line / path mapping ==========================
class Mapper:
    def __init__(self, path_mappings, project_mappings, combined_map_globs):
        self.path_mappings = path_mappings or []
        self.project_mappings = project_mappings or []
        # combined basename -> list of segments {file, base, start, lines}
        self.combined = {}
        # source basename -> (combined_basename, segment)
        self.by_source = {}
        for pattern in (combined_map_globs or []):
            for d in glob.glob(pattern):
                if os.path.isdir(d):
                    for mf in glob.glob(os.path.join(d, "*_combined.map.json")):
                        self._load_map(mf)
                elif d.endswith(".map.json"):
                    self._load_map(d)

    def _load_map(self, mf):
        try:
            with open(mf) as f:
                segs = json.load(f)
        except OSError:
            return
        comb_base = os.path.basename(mf).replace(".map.json", ".rb")
        lst = []
        for s in segs:
            seg = {"file": s["file"], "base": os.path.basename(s["file"]),
                   "start": s["start_line"], "lines": s["lines"]}
            lst.append(seg)
            self.by_source[seg["base"]] = (comb_base, seg)
        self.combined[comb_base] = lst

    def _apply(self, mappings, path, to_local):
        for m in mappings:
            a, b = (m["device"], m["local"]) if to_local else (m["local"], m["device"])
            if path.startswith(a):
                return b + path[len(a):]
        return path

    # device (file, line) -> local source path + original line (for stackTrace)
    def to_source(self, device_file, line):
        base = os.path.basename(device_file)
        if base in self.combined:
            for seg in self.combined[base]:
                if seg["start"] <= line < seg["start"] + seg["lines"]:
                    local = self._apply(self.project_mappings, seg["file"], True)
                    return local, line - seg["start"] + 1
            # fell through: unknown line in a combined file
            return device_file, line
        return self._apply(self.path_mappings, device_file, True), line

    # local source path + line -> device (file, line) for setBreakpoints
    def to_device(self, source_path, line):
        base = os.path.basename(source_path)
        if base in self.by_source:
            comb_base, seg = self.by_source[base]
            return comb_base, seg["start"] + line - 1
        # standalone file: device matches by basename
        return base, line


# ============================ DAP transport ================================
class DapIO:
    """Content-Length framed JSON over stdio (binary stdin/stdout)."""
    def __init__(self):
        self._in = sys.stdin.buffer
        self._out = sys.stdout.buffer
        self._wlock = threading.Lock()

    def read(self):
        headers = {}
        while True:
            line = self._in.readline()
            if not line:
                return None
            line = line.strip()
            if not line:
                break
            if b":" in line:
                k, v = line.split(b":", 1)
                headers[k.strip().lower()] = v.strip()
        n = int(headers.get(b"content-length", b"0"))
        body = self._in.read(n)
        return json.loads(body.decode("utf-8"))

    def write(self, msg):
        data = json.dumps(msg).encode("utf-8")
        with self._wlock:
            self._out.write(b"Content-Length: %d\r\n\r\n" % len(data))
            self._out.write(data)
            self._out.flush()


# ============================ adapter ======================================
class DapAdapter:
    def __init__(self):
        self.io = DapIO()
        self.seq = 0
        self.client = None
        self.mapper = None
        self.pid = None
        self.app_name = "vm"
        self._bp_seq = 0

    # --- outbound helpers ---
    def _next_seq(self):
        self.seq += 1
        return self.seq

    def respond(self, req, body=None, success=True, message=None):
        msg = {"seq": self._next_seq(), "type": "response",
               "request_seq": req["seq"], "success": success,
               "command": req["command"]}
        if body is not None:
            msg["body"] = body
        if message:
            msg["message"] = message
        self.io.write(msg)

    def event(self, name, body=None):
        msg = {"seq": self._next_seq(), "type": "event", "event": name}
        if body is not None:
            msg["body"] = body
        self.io.write(msg)

    # --- fmrb event callback (reader thread) ---
    def on_fmrb_event(self, name, payload):
        if name == "stopped":
            reason = payload.get("reason", "breakpoint")
            dap_reason = {"breakpoint": "breakpoint", "step": "step",
                          "pause": "pause"}.get(reason, reason)
            self.event("stopped", {"reason": dap_reason,
                                    "threadId": self.pid or 1,
                                    "allThreadsStopped": True})
        elif name == "resumed":
            self.event("continued", {"threadId": self.pid or 1,
                                     "allThreadsContinued": True})
        elif name == "exited":
            self.event("thread", {"reason": "exited", "threadId": self.pid or 1})
            self.event("terminated")

    # --- request dispatch ---
    def handle(self, req):
        cmd = req.get("command")
        fn = getattr(self, "req_" + cmd, None)
        if fn is None:
            self.respond(req, success=False, message=f"unsupported: {cmd}")
            return
        try:
            fn(req)
        except (FmrbDebugError, TimeoutError) as e:
            self.respond(req, success=False, message=str(e))

    # --- requests ---
    def req_initialize(self, req):
        self.respond(req, {
            "supportsConfigurationDoneRequest": True,
            "supportsStepInTargetsRequest": False,
            "supportsSingleThreadExecutionRequests": True,
        })
        self.event("initialized")

    def req_attach(self, req):
        args = req.get("arguments", {})
        host = args.get("host", "localhost")
        port = int(args.get("port", 5555))
        self.mapper = Mapper(args.get("pathMappings"),
                             args.get("projectMappings"),
                             args.get("combinedMaps"))
        self.client = FmrbDebugClient(host, port, on_event=self.on_fmrb_event)
        self.client.connect()

        target = args.get("app")
        apps = self.client.ps()
        pid = None
        if isinstance(target, int) or (isinstance(target, str) and target.isdigit()):
            pid = int(target)
        else:
            for a in apps:
                if a["name"] == target:
                    pid = a["pid"]
                    self.app_name = a["name"]
                    break
        if pid is None:
            self.respond(req, success=False, message=f"app {target!r} not found")
            return
        self.pid = pid
        for a in apps:
            if a["pid"] == pid:
                self.app_name = a["name"]
        self.client.attach(pid)
        self.respond(req)

    def req_setBreakpoints(self, req):
        args = req["arguments"]
        src = args.get("source", {})
        path = src.get("path", "")
        bps = args.get("breakpoints", [])
        verified = []
        # Clear then re-set (simple full-replace per source is DAP-friendly, but
        # our device API has no per-source clear; clear all then set requested).
        # We keep it minimal: set each; device de-dups by (file,line) loosely.
        for b in bps:
            line = b["line"]
            dev_file, dev_line = self.mapper.to_device(path, line)
            try:
                bp_id = self.client.bp_set(self.pid, dev_file, dev_line)
                verified.append({"verified": True, "line": line, "id": bp_id})
            except FmrbDebugError:
                verified.append({"verified": False, "line": line})
        self.respond(req, {"breakpoints": verified})

    def req_configurationDone(self, req):
        self.respond(req)

    def req_threads(self, req):
        self.respond(req, {"threads": [{"id": self.pid or 1, "name": self.app_name}]})

    def req_stackTrace(self, req):
        frames = self.client.stack_trace(self.pid)
        out = []
        for f in frames:
            dev_file = f.get("file", "")
            line = f.get("line", -1)
            src = None
            if dev_file and line >= 0:
                local, oline = self.mapper.to_source(dev_file, line)
                src = {"name": os.path.basename(local), "path": local}
                line = oline
            out.append({
                "id": f["idx"],
                "name": f.get("func", "?"),
                "line": max(line, 0),
                "column": 1,
                "source": src,
            })
        self.respond(req, {"stackFrames": out, "totalFrames": len(out)})

    def req_scopes(self, req):
        frame_id = req["arguments"]["frameId"]
        # Encode the frame index into the variablesReference (frame_id+1).
        self.respond(req, {"scopes": [{
            "name": "Locals",
            "variablesReference": frame_id + 1,
            "expensive": False,
        }]})

    def req_variables(self, req):
        ref = req["arguments"]["variablesReference"]
        frame = ref - 1
        vs = self.client.frame_vars(self.pid, frame)
        out = [{"name": v["name"], "value": v["value"],
                "type": v.get("type", ""), "variablesReference": 0} for v in vs]
        self.respond(req, {"variables": out})

    def req_continue(self, req):
        self.client.cont(self.pid)
        self.respond(req, {"allThreadsContinued": True})

    def req_next(self, req):
        self.client.step(self.pid, "over")
        self.respond(req)

    def req_stepIn(self, req):
        self.client.step(self.pid, "in")
        self.respond(req)

    def req_stepOut(self, req):
        self.client.step(self.pid, "out")
        self.respond(req)

    def req_pause(self, req):
        self.client.pause(self.pid)
        self.respond(req)

    def req_disconnect(self, req):
        try:
            if self.client and self.pid is not None:
                self.client.detach(self.pid)
        except (FmrbDebugError, TimeoutError):
            pass
        self.respond(req)
        if self.client:
            self.client.close()

    def req_source(self, req):
        # We always provide source.path, so this is rarely called.
        self.respond(req, {"content": ""})

    # --- main loop ---
    def run(self):
        while True:
            req = self.io.read()
            if req is None:
                break
            if req.get("type") == "request":
                self.handle(req)
                if req.get("command") == "disconnect":
                    break


def main():
    DapAdapter().run()


if __name__ == "__main__":
    main()

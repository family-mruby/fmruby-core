# Phase 5 check: everything the Python framework gained in one small app.
#
#   - subscribe / publish, including messages from a Ruby app (press R to start
#     the Ruby publisher, then click its button)
#   - a timer that keeps running while the app waits for messages
#   - import: the drawing half lives in pysub_panel.py next to this file
#   - read_file: the app reads its own .app.toml
#   - suspend / resume and the fullscreen switch (on_resize)

import _fmrb
import pysub_panel

TOPIC = "demo"
MY_TOML = "/app/python/pysub.app.toml"
PUBLISHER = "/app/demo/pub_demo.app.rb"
SUBSCRIBER = "/app/demo/sub_demo.app.rb"
# The Ruby robot world publishes a state whose "view" is a list of lists, which
# is the shape phase7 has to carry across the language boundary.
EXPLORER = "/app/game/robo_explorer/robo_explorer.app.rb"
TOPIC_ROBO = "robo/state"


class PySubApp(FmrbApp):
    def on_create(self):
        self.state = {
            "count": 0,
            "last": None,
            "toml_bytes": 0,
            "uptime": 0,
            "sent": 0,
            "view": "-",
            "suspended": False,
            "blink": False,
        }
        self.started_at = ticks_ms()
        self.sent = 0
        self.run_pids = {}

        data = _fmrb.read_file(MY_TOML)
        self.state["toml_bytes"] = len(data) if data else -1

        self.subscribe(TOPIC)
        self.subscribe(TOPIC_ROBO)
        self.blink()
        self.redraw()

    def on_destroy(self):
        self.unsubscribe(TOPIC)
        self.unsubscribe(TOPIC_ROBO)

    # One-shot timers, so the callback arms the next one. Twice a second is
    # slower than the app's own turn, which is the point: the dot has to keep
    # flashing even while spin() is parked waiting for a message.
    def blink(self):
        self.state["blink"] = not self.state["blink"]
        self.set_timer(500, self.blink)
        if self.running and not self._suspended:
            self.redraw()

    def redraw(self):
        self.state["uptime"] = ticks_ms() - self.started_at
        pysub_panel.draw(self, self.state)

    def on_control(self, msg):
        if msg.get("cmd") == "topic_data" and msg.get("topic") == TOPIC:
            data = msg.get("data")
            if data:
                self.state["count"] += 1
                self.state["last"] = data
                Log.info("pysub: got " + str(data))
                self.redraw()
        elif msg.get("cmd") == "topic_data" and msg.get("topic") == TOPIC_ROBO:
            view = msg.get("data", {}).get("view")
            if view is not None:
                # Length of the corridor, then the first cell as it arrived:
                # [left_wall, right_wall, kind]. Printing it whole is the check
                # -- a list of lists has to survive the trip intact.
                self.state["view"] = str(len(view)) + ":" + str(view[0] if view else "-")
                self.redraw()
        elif msg.get("cmd") == "run_result":
            path = msg.get("path")
            self.run_pids[path] = msg.get("pid")
            Log.info("pysub: ran " + str(path) + " -> pid " + str(msg.get("pid")))

    def on_event(self, ev):
        super().on_event(ev)
        if ev.get("type") != "key_down":
            return
        # Match on the scancode (HID usage id): keycode differs between the
        # simulation and the device.
        sc = ev.get("scancode")
        if sc == 0x15:      # R: the Ruby publisher, so Ruby -> Python can be seen
            self.request_run(PUBLISHER, self.run_pids.get(PUBLISHER))
        elif sc == 0x16:    # S: the Ruby subscriber, for Python -> Ruby
            self.request_run(SUBSCRIBER, self.run_pids.get(SUBSCRIBER))
        elif sc == 0x13:    # P: publish, with a list in the payload
            # The kernel does not echo a publish back to its publisher, so what
            # this proves is on the Ruby subscriber's screen: if the list failed
            # to pack, the whole message would fail to decode there.
            self.sent += 1
            self.state["sent"] = self.sent
            self.redraw()
            self.publish(TOPIC, {"msg": "python", "n": self.sent,
                                 "list": [1, 2, 3]})
        elif sc == 0x08:    # E: the Ruby robot world, whose state carries lists
            self.request_run(EXPLORER, self.run_pids.get(EXPLORER))
        elif sc == 0x09:    # F
            self.toggle_fullscreen()

    def on_suspend(self):
        self.state["suspended"] = True

    def on_resume(self):
        self.state["suspended"] = False
        self.redraw()

    def on_resize(self, width, height):
        self.redraw()

    def on_update(self):
        return 200


app = PySubApp()
app.start()

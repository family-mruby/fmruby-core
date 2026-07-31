# Checks for the Python guest VM that the demo app does not cover. Lives under
# /home rather than /app so the launcher does not list it: it is a measuring
# tool, not something to browse to. Start it with
#   run /home/test/pytest.app.py
#
# What it exercises:
#   - plain script execution before any framework object exists (the prints)
#   - a long on_update interval, so the app spends its life blocked inside
#     _fmrb.spin: a stop request has to break that wait rather than be noticed
#     500ms later when the next turn happens to come round
#   - a busy loop on demand, for the other stop path (the VM hook aborting
#     bytecode execution)

print("pytest: start")
print("pytest: squares", sum(x * x for x in range(100)))
print("pytest: chars", [c for c in "fmrb"])
print("pytest: float", 1.0 / 8)


class PyTestApp(FmrbApp):
    # Long enough that a stop arriving mid-wait is obviously not being handled
    # by the next turn of the loop.
    IDLE_MS = 5000

    def __init__(self):
        super().__init__()
        self.turns = 0

    def on_create(self):
        Log.info("pytest: on_create")
        self.draw_window_frame()
        self.redraw()

    def on_update(self):
        self.turns += 1
        Log.info("pytest: turn " + str(self.turns))
        self.redraw()
        return self.IDLE_MS

    def on_event(self, ev):
        super().on_event(ev)
        if not self.running:
            return
        # A click in the user area starts a long busy loop, so the other stop
        # path can be tried: this one is interrupted by the VM hook, not by the
        # spin wait ending.
        if ev.get("type") == "mouse_up" and ev.get("y", 0) >= self.user_area_y0:
            Log.info("pytest: busy loop start")
            total = 0
            i = 0
            while i < 40000000:
                total += i
                i += 1
            Log.info("pytest: busy loop done " + str(total))

    def redraw(self):
        self.clear_user_area(FmrbGfx.BLACK)
        g = self.gfx
        x0 = self.user_area_x0
        y0 = self.user_area_y0
        g.draw_text(x0 + 4, y0 + 4, "turn " + str(self.turns), FmrbGfx.WHITE)
        g.draw_text(x0 + 4, y0 + 18, "idle " + str(self.IDLE_MS) + "ms", FmrbGfx.GRAY)
        g.draw_text(x0 + 4, y0 + 32, "click = busy loop", FmrbGfx.CYAN)
        g.present()

    def on_destroy(self):
        Log.info("pytest: on_destroy after " + str(self.turns) + " turns")


PyTestApp().start()
print("pytest: end")

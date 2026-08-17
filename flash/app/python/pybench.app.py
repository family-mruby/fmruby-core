# P5-0: what fits in one frame of a Python app.
#
# Three things get measured, because they are the three that decide how a game
# is written (doc/micropython/phase5.md):
#
#   1. raw execution speed  -- how much logic fits in a turn
#   2. drawing commands     -- redraw everything, or only what changed
#   3. GC pauses            -- the worst single stall the player can feel
#
# Results go to the log (and the screen), so this runs headless.

import _fmrb
import gc

REPEAT = 200000
DRAW_N = 200


def bench(name, fn, count):
    """Run fn count times, report the per-call cost in microseconds."""
    gc.collect()
    t0 = ticks_ms()
    fn(count)
    ms = ticks_ms() - t0
    # Nanoseconds per call: on the simulation host a bytecode operation is well
    # under a microsecond, and microsecond resolution would print 0 for all of
    # them. The device numbers are the ones that matter, and they are ~100x
    # larger, but the same unit keeps the two tables comparable.
    per_ns = (ms * 1000000) // count if count else 0
    Log.info("pybench: %-14s %6d ms / %d = %d ns" % (name, ms, count, per_ns))
    return (name, ms, per_ns)


class Dummy:
    def __init__(self):
        self.v = 0

    def bump(self):
        self.v += 1


class PyBenchApp(FmrbApp):
    def on_create(self):
        self.rows = []
        self.done = False

    def on_update(self):
        if self.done:
            return 500
        self.done = True
        self.run_all()
        return 500

    def run_all(self):
        Log.info("pybench: start (platform " + str(self.platform) + ")")

        self.rows.append(bench("empty loop", self._empty, REPEAT))
        self.rows.append(bench("int math", self._math, REPEAT))
        self.rows.append(bench("attribute", self._attr, REPEAT))
        self.rows.append(bench("method call", self._call, REPEAT))
        self.rows.append(bench("list append", self._list, REPEAT))
        self.rows.append(bench("dict get", self._dict, REPEAT))
        self.rows.append(bench("fill_rect", self._draw, DRAW_N))
        self.rows.append(bench("present", self._present, 60))
        self.bench_sprites()

        self.measure_gc()
        self.show()
        Log.info("pybench: done")

    # ---- the loops ----

    def _empty(self, n):
        i = 0
        while i < n:
            i += 1

    def _math(self, n):
        i = 0
        acc = 0
        while i < n:
            acc = (acc + i * 3) % 1000
            i += 1

    def _attr(self, n):
        d = Dummy()
        i = 0
        while i < n:
            d.v = d.v + 1
            i += 1

    def _call(self, n):
        d = Dummy()
        i = 0
        while i < n:
            d.bump()
            i += 1

    def _list(self, n):
        i = 0
        buf = []
        while i < n:
            buf.append(i)
            if len(buf) > 64:
                buf = []
            i += 1

    def _dict(self, n):
        d = {"x": 1, "y": 2, "kind": "floor"}
        i = 0
        acc = 0
        while i < n:
            acc += d["x"]
            i += 1

    def _draw(self, n):
        i = 0
        while i < n:
            self.gfx.fill_rect(2 + (i % 40), 20 + (i % 30), 4, 4, i & 0xFF)
            i += 1

    def _present(self, n):
        i = 0
        while i < n:
            self.gfx.present()
            i += 1

    # What a frame of a sprite-driven game costs: move every sprite, then
    # present once. This is the shape phase9 is planned around, so the number
    # per sprite is what decides how many can be on screen.
    def bench_sprites(self):
        images = []
        sprites = []
        for i in range(8):
            img = SpriteImage(self.gfx, 8, 8, transparent_color=0, use_transparent=True)
            img.set_target()
            self.gfx.fill_rect(0, 0, 8, 8, 0x1C + i)
            img.reset_target()
            images.append(img)
            sprites.append(SpriteInstance(self.gfx, img, 10 + i * 10, 100, 1))

        frames = 60
        t0 = ticks_ms()
        for f in range(frames):
            y = 100 + (f % 8)
            for i in range(8):
                sprites[i].move(10 + i * 10, y)
            self.gfx.present()
        ms = ticks_ms() - t0
        per_frame_us = (ms * 1000) // frames
        Log.info("pybench: %-14s %6d ms / %d frames = %d us/frame (8 sprites)"
                 % ("sprite frame", ms, frames, per_frame_us))
        self.rows.append(("sprite frame", ms, per_frame_us * 1000 // 1000))

        for s in sprites:
            s.destroy()
        for i in images:
            i.destroy()

    # The worst pause a collection can cause, measured with the heap filled the
    # way a running app fills it: many small live objects, plus garbage.
    def measure_gc(self):
        keep = []
        for i in range(2000):
            keep.append([i, i + 1, "cell"])
        for i in range(4000):
            tmp = {"a": i, "b": str(i)}

        t0 = ticks_ms()
        gc.collect()
        pause = ticks_ms() - t0
        free = gc.mem_free()
        Log.info("pybench: gc.collect %d ms (live 2000 lists, free %d)" % (pause, free))
        self.rows.append(("gc pause", pause, 0))
        self.keep = keep

    def show(self):
        self.clear_user_area()
        y = self.user_area_y0 + 2
        self.gfx.draw_text(self.user_area_x0 + 3, y, "pybench (ns/call)", 0xFC)
        y += 10
        for name, ms, per_ns in self.rows:
            self.gfx.draw_text(self.user_area_x0 + 3, y, name, 0x6D)
            self.gfx.draw_text(self.user_area_x0 + 100, y,
                               str(per_ns) if per_ns else str(ms) + "ms", 0xFF)
            y += 9
        self.draw_window_frame()
        self.gfx.present()


app = PyBenchApp()
app.start()

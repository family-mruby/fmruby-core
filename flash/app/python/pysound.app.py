# Phase 8 check: the internal sound chip from Python.
#
#   1  a scale on pulse 1, one note every 200 ms
#   2  the tune (korobeiniki.fmsq) on MAIN
#   3  effects on SUB while the tune keeps playing
#   4  send 200 notes and report what they allocated (it must be nothing)
#   0  stop everything
#
# Effects are switched off by a timer measured in real time, not in frames:
# a slow redraw would otherwise stretch a note (see the note in
# doc/micropython/phase8.md).

import gc

CACHE_DIR = "/cache/app/pysound"
TUNE_SRC = "/usr/share/music/korobeiniki.fmsq"
TUNE_SLOT = 1

SCALE = (262, 294, 330, 349, 392, 440, 494, 523)   # C4 to C5

COL_TEXT = 0xFF
COL_DIM = 0x6D
COL_ON = 0x1C
COL_KEY = 0xFC


class PySoundApp(FmrbApp):
    def on_create(self):
        self.audio = FmrbAudio(self)
        self.status = "ready"
        self.tune_on = False
        self.note_i = -1
        # Channel -> the time its note must be switched off at.
        self.note_off_at = {}

        self.gfx.sync_file(TUNE_SRC, CACHE_DIR + "/korobeiniki.fmsq")
        self.audio.load_fmsq_file(TUNE_SLOT, CACHE_DIR + "/korobeiniki.fmsq")
        self.redraw()

    def on_destroy(self):
        # Nothing may be left sounding after the window closes.
        self.silence()
        self.audio.stop()

    # ---- sound ----

    # Play a note now and book its end. The booking is by wall clock, so a
    # frame that takes longer than planned does not lengthen the note.
    def note(self, channel, freq, ms, volume=10, duty=2):
        self.audio.note_on(channel, freq, volume, duty, 0)
        self.note_off_at[channel] = ticks_ms() + ms

    def tick_notes(self):
        if not self.note_off_at:
            return
        now = ticks_ms()
        done = None
        for ch in self.note_off_at:
            if self.note_off_at[ch] <= now:
                if done is None:
                    done = [ch]
                else:
                    done.append(ch)
        if done is None:
            return
        for ch in done:
            self.audio.note_off(ch)
            del self.note_off_at[ch]

    def silence(self):
        for ch in list(self.note_off_at):
            self.audio.note_off(ch)
        del self.note_off_at
        self.note_off_at = {}

    def start_scale(self):
        self.note_i = 0
        self.status = "scale"

    def step_scale(self):
        if self.note_i < 0:
            return
        if self.note_i >= len(SCALE):
            self.note_i = -1
            self.status = "scale done"
            self.redraw()
            return
        self.note(FmrbAudio.CH_PULSE1, SCALE[self.note_i], 150, 12)
        self.note_i += 1

    def toggle_tune(self):
        if self.tune_on:
            self.audio.stop()
            self.tune_on = False
            self.status = "tune stopped"
        else:
            self.audio.play_slot(TUNE_SLOT, FmrbAudio.MAIN)
            self.tune_on = True
            self.status = "tune on MAIN"

    # An effect on SUB: the tune on MAIN has to keep playing through it.
    def effect(self):
        self.note(FmrbAudio.CH_PULSE2, 988, 60, 14, 2)
        self.status = "effect on SUB"

    # ---- app ----

    def on_update(self):
        self.tick_notes()
        if self.note_i >= 0:
            self.step_scale()
            self.redraw()
            return 200
        return 100

    def on_event(self, ev):
        super().on_event(ev)
        if ev.get("type") != "key_down":
            return
        sc = ev.get("scancode")
        if sc == 0x1E:      # 1
            self.start_scale()
        elif sc == 0x1F:    # 2
            self.toggle_tune()
        elif sc == 0x20:    # 3
            self.effect()
        elif sc == 0x21:    # 4
            self.check_alloc()
        elif sc == 0x27:    # 0
            self.silence()
            self.audio.stop()
            self.tune_on = False
            self.note_i = -1
            self.status = "stopped"
        else:
            return
        self.redraw()

    # A note in a stream must not allocate: on the device a collection stops
    # the app for 100-205 ms, which is audible in the middle of a tune. The
    # notes are silent (volume 0 on a channel nothing else uses) so this can
    # run while the tune plays.
    def check_alloc(self):
        gc.collect()
        before = gc.mem_alloc()
        i = 0
        while i < 100:
            self.audio.note_on(FmrbAudio.CH_NOISE, 100, 0, 0, 0)
            self.audio.note_off(FmrbAudio.CH_NOISE)
            i += 1
        used = gc.mem_alloc() - before
        Log.info("pysound: 200 notes allocated " + str(used) + " bytes")
        self.status = "200 notes: " + str(used) + " B"

    def redraw(self):
        g = self.gfx
        self.clear_user_area()
        x = self.user_area_x0 + 3
        y = self.user_area_y0 + 3
        g.draw_text(x, y, "pysound", COL_KEY)
        y += 11
        g.draw_text(x, y, self.status, COL_ON if self.tune_on else COL_TEXT)
        y += 11
        note = SCALE[self.note_i - 1] if self.note_i > 0 else 0
        g.draw_text(x, y, "note " + (str(note) + " Hz" if note else "-"), COL_TEXT)
        y += 14
        g.draw_text(x, y, "1: scale", COL_DIM)
        y += 10
        g.draw_text(x, y, "2: tune (MAIN)", COL_DIM)
        y += 10
        g.draw_text(x, y, "3: effect (SUB)", COL_DIM)
        y += 10
        g.draw_text(x, y, "0: stop", COL_DIM)
        self.draw_window_frame()
        g.present()


app = PySoundApp()
app.start()

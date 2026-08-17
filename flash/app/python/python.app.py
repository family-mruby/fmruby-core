# Python demo -- everything the Python app framework offers, one page at a time.
#
#   1-5 or a click   change page
#   the page's own keys are listed at the bottom of each page
#
# Written the way any app is: subclass FmrbApp, override the lifecycle methods,
# instantiate and start. The framework classes (FmrbApp, FmrbGfx, FmrbAudio,
# SpriteImage, SpriteInstance, Log, ticks_ms, language) are already in this
# namespace; only _fmrb and files of your own need importing.

import random

import _fmrb
import python_status          # the system page's text, in a file of its own

RPG_DIR = "/app/game/rpg_demo"
CACHE = "/cache/app/python"
TUNE_SRC = "/usr/share/music/korobeiniki.fmsq"
TUNE_SLOT = 2
MY_TOML = "/app/python/python.app.toml"
TOPIC = "demo"
PUBLISHER = "/app/demo/pub_demo.app.rb"

SAMPLE_JA = "こんにちは ABC 日本語"
SCALE = (262, 294, 330, 349, 392, 440, 494, 523)

PAGES = ("Shapes", "Text", "Sprites", "Sound", "System")
KEYS = ("click or 1-5 for pages",
        "T: font",
        "arrows: move  SPACE: frame  I: image",
        "1..3 keys below",
        "R: run Ruby publisher  P: publish")


class PythonDemoApp(FmrbApp):
    def on_create(self):
        Log.info("Python demo started on " + self.platform)
        self.page = 0
        self.blink = False
        self.sprites_ready = False

        # Page state, kept here so a page redraw is cheap.
        self.font_mode = 0
        self.frame = 0
        self.sprite_x = 0
        self.sprite_y = 0
        self.image = None
        self.audio = FmrbAudio(self)
        self.note_off_at = {}
        self.tune_on = False
        self.received = None
        self.pub_pid = None
        self.sent = 0

        self.started_at = ticks_ms()
        self.uptime = 0
        self.lang = language()
        self.note_i = -1
        self.toml_bytes = len(_fmrb.read_file(MY_TOML) or "")
        self.subscribe(TOPIC)
        self.tick_blink()
        self.draw_page()

    def on_destroy(self):
        self.unsubscribe(TOPIC)
        self.silence()
        self.audio.stop()

    # ---- the page frame ----

    def draw_page(self):
        g = self.gfx
        self.clear_user_area(FmrbGfx.BLACK)
        x0 = self.user_area_x0
        y0 = self.user_area_y0

        g.draw_text(x0 + 4, y0 + 2,
                    str(self.page + 1) + "/" + str(len(PAGES)) + " " + PAGES[self.page],
                    FmrbGfx.YELLOW)

        if self.page == 0:
            self.draw_shapes(x0, y0 + 14)
        elif self.page == 1:
            self.draw_text_page(x0, y0 + 14)
        elif self.page == 2:
            self.draw_sprite_page(x0, y0 + 14)
        elif self.page == 3:
            self.draw_sound_page(x0, y0 + 14)
        else:
            self.draw_system_page(x0, y0 + 14)

        g.draw_text(x0 + 4, self.user_area_y1 - 10, KEYS[self.page], FmrbGfx.GRAY)
        self.draw_window_frame()
        g.present()

    # Only the sprite page keeps sprites on screen; leaving them visible on the
    # other pages would draw them over whatever is there.
    def show_sprites(self, on):
        if not self.sprites_ready:
            return
        self.player.set_visible(on)

    # ---- page 1: shapes ----

    def draw_shapes(self, x0, y0):
        g = self.gfx
        g.draw_round_rect(x0 + 6, y0 + 4, 56, 26, 6, FmrbGfx.CYAN)
        g.fill_round_rect(x0 + 70, y0 + 4, 56, 26, 6, FmrbGfx.GREEN)
        g.draw_ellipse(x0 + 34, y0 + 52, 26, 14, FmrbGfx.MAGENTA)
        g.fill_ellipse(x0 + 98, y0 + 52, 26, 14, FmrbGfx.RED)
        g.draw_triangle(x0 + 8, y0 + 104, x0 + 60, y0 + 104, x0 + 34, y0 + 74,
                        FmrbGfx.YELLOW)
        g.fill_triangle(x0 + 72, y0 + 104, x0 + 124, y0 + 104, x0 + 98, y0 + 74,
                        FmrbGfx.BLUE)

        # A fan of lines, stepped with a list comprehension: the language is
        # part of what this demo shows.
        colours = [FmrbGfx.RED, FmrbGfx.YELLOW, FmrbGfx.GREEN, FmrbGfx.CYAN]
        cx = x0 + 160
        for i, dx in enumerate([-30, -10, 10, 30]):
            g.draw_line(cx, y0 + 104, cx + dx, y0 + 8, colours[i])
        for i in range(3):
            g.draw_circle(x0 + 140 + i * 22, y0 + 40, 9, FmrbGfx.WHITE)
            g.fill_circle(x0 + 140 + i * 22, y0 + 40, 4, FmrbGfx.GRAY)

    # ---- page 2: text ----

    def draw_text_page(self, x0, y0):
        g = self.gfx
        modes = ("default font", "ja font", "mixed (ascii + ja)")

        if self.font_mode == 0:
            g.set_font(FmrbGfx.FONT_DEFAULT)
            sample = "ABC abc 123"
        else:
            sample = SAMPLE_JA
            if self.font_mode == 1:
                g.set_font(FmrbGfx.FONT_JA, 12)

        mixed = self.font_mode == 2
        g.draw_text(x0 + 6, y0 + 6, modes[self.font_mode], FmrbGfx.GRAY)
        g.draw_text(x0 + 6, y0 + 20, sample, FmrbGfx.CYAN, None, mixed)

        # text_width walks UTF-8, so the rule under the line ends exactly where
        # the text does -- which is the point of drawing it.
        width = g.text_width(sample)
        g.draw_line(x0 + 6, y0 + 34, x0 + 6 + width - 1, y0 + 34, FmrbGfx.RED)
        g.set_font(FmrbGfx.FONT_DEFAULT)
        g.draw_text(x0 + 6, y0 + 40,
                    "width " + str(width) + " px, len " + str(len(sample)) + " bytes",
                    FmrbGfx.GRAY)

        rows = (
            "language() = " + language(),
            "sum(1..10) = " + str(sum(range(1, 11))),
            "2 ** 16 = " + str(2 ** 16),
            "sorted = " + str(sorted([3, 1, 2])),
            "canvas " + str(self.window_width) + "x" + str(self.window_height),
        )
        for i, row in enumerate(rows):
            g.draw_text(x0 + 6, y0 + 58 + i * 10, row, FmrbGfx.WHITE)

    # ---- page 3: sprites, images, tiles ----

    def load_sprites(self):
        if self.sprites_ready:
            return
        names = ("player_down_0.bmp", "player_down_1.bmp", "world.bmp")
        for name in names:
            self.gfx.sync_file(RPG_DIR + "/" + name, CACHE + "/" + name)

        self.frames = []
        for name in ("player_down_0.bmp", "player_down_1.bmp"):
            img = SpriteImage(self.gfx, 16, 16, transparent_color=0,
                              use_transparent=True)
            img.load_bmp(CACHE + "/" + name)
            self.frames.append(img)
        self.player = SpriteInstance(self.gfx, self.frames, 0, 0, 1)

        # A tile sheet stays an image: draw_tile stamps rectangles out of it
        # without an instance per tile.
        self.sheet = SpriteImage(self.gfx, 128, 128, transparent_color=0)
        self.sheet.load_bmp(CACHE + "/world.bmp")

        # An image drawn from Python rather than loaded.
        self.badge = SpriteImage(self.gfx, 20, 12)
        self.badge.set_target()
        self.gfx.fill_rect(0, 0, 20, 12, FmrbGfx.BLUE)
        self.gfx.draw_rect(0, 0, 20, 12, FmrbGfx.WHITE)
        self.gfx.draw_text(3, 2, "PY", FmrbGfx.WHITE)
        self.badge.reset_target()

        self.sprites_ready = True

    def draw_sprite_page(self, x0, y0):
        self.load_sprites()
        g = self.gfx
        for i in range(6):
            g.draw_tile(self.sheet.id, (i % 4) * 16, 0, 16, 16, x0 + 6 + i * 16, y0 + 4)
        g.draw_tile(self.badge.id, 0, 0, 20, 12, self.user_area_x1 - 26, y0 + 6)

        g.draw_text(x0 + 6, y0 + 26, "sprite: move and frame", FmrbGfx.GRAY)
        g.draw_text(x0 + 6, y0 + 74, "I: a decoded picture ->", FmrbGfx.GRAY)

        if self.sprite_x == 0:
            self.sprite_x = x0 + 20
            self.sprite_y = y0 + 40
        self.player.move(self.sprite_x, self.sprite_y)
        self.show_sprites(True)

        if self.image:
            # A quarter size and off to the right, so the picture sits beside
            # the sprite rather than over the labels.
            g.draw_image(self.image["id"], self.user_area_x1 - 84, y0 + 40, 0.25)

    # create_image decodes a picture file (a PNG); a sprite BMP belongs in
    # load_bmp instead. Handing this one a BMP gives an empty image, silently.
    def toggle_image(self):
        if self.image:
            self.gfx.delete_image(self.image["id"])
            self.image = None
        else:
            self.image = self.gfx.create_image("/data/bg_426x240.png")
            if not self.image:
                Log.warn("python demo: create_image failed")
        self.draw_page()

    # ---- page 4: sound ----

    def draw_sound_page(self, x0, y0):
        g = self.gfx
        rows = (
            ("1: scale on pulse1", FmrbGfx.WHITE),
            ("2: tune on MAIN " + ("(playing)" if self.tune_on else ""), FmrbGfx.WHITE),
            ("3: effect on SUB", FmrbGfx.WHITE),
            ("0: stop everything", FmrbGfx.WHITE),
            ("", FmrbGfx.GRAY),
            ("a note allocates nothing:", FmrbGfx.GRAY),
            ("it goes straight to C", FmrbGfx.GRAY),
        )
        for i, entry in enumerate(rows):
            text, colour = entry
            if text:
                g.draw_text(x0 + 6, y0 + 6 + i * 12, text, colour)

    def play_scale(self):
        self.note_i = 0
        self.scale_at = ticks_ms()

    def tick_scale(self):
        if self.note_i < 0:
            return
        if ticks_ms() < self.scale_at:
            return
        if self.note_i >= len(SCALE):
            self.note_i = -1
            return
        self.note(FmrbAudio.CH_PULSE1, SCALE[self.note_i], 150, 12)
        self.note_i += 1
        self.scale_at = ticks_ms() + 200

    def toggle_tune(self):
        if self.tune_on:
            self.audio.stop()
            self.tune_on = False
        else:
            self.gfx.sync_file(TUNE_SRC, CACHE + "/tune.fmsq")
            self.audio.load_fmsq_file(TUNE_SLOT, CACHE + "/tune.fmsq")
            self.audio.play_slot(TUNE_SLOT, FmrbAudio.MAIN)
            self.tune_on = True
        self.draw_page()

    # A note plays until it is switched off, so the time to stop it is booked
    # by the clock -- not counted in frames, which stretch when drawing is slow.
    def note(self, channel, freq, ms, volume=12, duty=2):
        self.audio.note_on(channel, freq, volume, duty, 0)
        self.note_off_at[channel] = ticks_ms() + ms

    def tick_notes(self):
        if not self.note_off_at:
            return
        now = ticks_ms()
        due = None
        for ch in self.note_off_at:
            if self.note_off_at[ch] <= now:
                due = [ch] if due is None else due + [ch]
        if due is None:
            return
        for ch in due:
            self.audio.note_off(ch)
            del self.note_off_at[ch]

    def silence(self):
        for ch in list(self.note_off_at):
            self.audio.note_off(ch)
        self.note_off_at = {}

    # ---- page 5: the system side ----

    def draw_system_page(self, x0, y0):
        g = self.gfx
        self.uptime = ticks_ms() - self.started_at
        # The rows come from python_status.py: a module an app imports cannot
        # see FmrbApp or FmrbGfx, so it is handed what it needs -- here the app
        # itself -- and returns text for the app to draw.
        for i, entry in enumerate(python_status.rows(self)):
            label, value = entry
            g.draw_text(x0 + 6, y0 + 6 + i * 11, label, FmrbGfx.GRAY)
            g.draw_text(x0 + 86, y0 + 6 + i * 11, value, FmrbGfx.WHITE)
        if self.blink:
            g.fill_circle(self.user_area_x1 - 10, y0 + 8, 3, FmrbGfx.GREEN)

    # One-shot timers: the callback arms the next one. They also run inside a
    # wait, which is why the dot keeps flashing while nothing else happens.
    def tick_blink(self):
        self.blink = not self.blink
        self.set_timer(500, self.tick_blink)
        if self.page == 4 and self.running:
            self.draw_page()

    def on_control(self, msg):
        if msg.get("cmd") == "topic_data" and msg.get("topic") == TOPIC:
            self.received = msg.get("data")
            if self.page == 4:
                self.draw_page()
        elif msg.get("cmd") == "run_result":
            self.pub_pid = msg.get("pid")

    # ---- lifecycle ----

    def on_update(self):
        self.tick_notes()
        self.tick_scale()
        return 100

    def on_resize(self, width, height):
        self.sprite_x = 0
        self.draw_page()

    def on_event(self, ev):
        super().on_event(ev)
        if not self.running:
            return
        kind = ev.get("type")

        if kind == "mouse_up" and ev.get("button") == 1:
            if ev.get("y", 0) >= self.user_area_y0:
                self.set_page((self.page + 1) % len(PAGES))
            return
        if kind != "key_down":
            return

        sc = ev.get("scancode")
        if 0x1E <= sc <= 0x22 and self.page != 3:      # 1-5 pick a page
            self.set_page(sc - 0x1E)
            return
        self.page_key(sc)

    def set_page(self, page):
        self.page = page
        self.show_sprites(page == 2)
        self.draw_page()

    def page_key(self, sc):
        if self.page == 1 and sc == 0x17:              # T
            self.font_mode = (self.font_mode + 1) % 3
            self.draw_page()
        elif self.page == 2:
            self.sprite_key(sc)
        elif self.page == 3:
            self.sound_key(sc)
        elif self.page == 4:
            self.system_key(sc)

    def sprite_key(self, sc):
        if sc == 0x2C:                                  # space: next frame
            self.frame = 1 - self.frame
            self.player.set_frame(self.frame)
            self.gfx.present()
        elif sc == 0x0C:                                # I: the image
            self.toggle_image()
        elif sc in (0x4F, 0x50, 0x51, 0x52):            # arrows
            self.sprite_x += 6 if sc == 0x4F else (-6 if sc == 0x50 else 0)
            self.sprite_y += 6 if sc == 0x51 else (-6 if sc == 0x52 else 0)
            self.player.move(self.sprite_x, self.sprite_y)
            self.gfx.present()

    def sound_key(self, sc):
        if sc == 0x1E:                                  # 1
            self.play_scale()
        elif sc == 0x1F:                                # 2
            self.toggle_tune()
        elif sc == 0x20:                                # 3
            self.note(FmrbAudio.CH_PULSE2, 988, 90, 14)
        elif sc == 0x27:                                # 0
            self.silence()
            self.audio.stop()
            self.tune_on = False
            self.note_i = -1
            self.draw_page()
        elif 0x21 <= sc <= 0x22:                        # 4-5 still change page
            self.set_page(sc - 0x1E)

    def system_key(self, sc):
        if sc == 0x15:                                  # R: the Ruby publisher
            # An app cannot spawn another app, so this asks the kernel to.
            self.request_run(PUBLISHER, self.pub_pid)
        elif sc == 0x13:                                # P: publish
            self.sent += 1
            self.publish(TOPIC, {"msg": "python", "n": self.sent,
                                 "list": [1, 2, 3]})
            self.draw_page()


PythonDemoApp().start()

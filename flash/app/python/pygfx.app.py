# Phase 6 check: Japanese text, images, sprites and tiles from Python.
#
# Assets are borrowed from the Ruby RPG demo rather than added again: the point
# is that the same files work from either language.
#
#   T  switch the sample line between the default font, the Japanese font
#      and the mixed one
#   I  load the tile sheet as an image and draw it scaled
#   SPACE  step the walking frame of the loaded sprite
#   arrow keys  move the sprite

import _fmrb

RPG_DIR = "/app/game/rpg_demo"
CACHE_DIR = "/cache/app/pygfx"
# Graphics-side path (that side's own filesystem), not a local one.
WALLPAPER = "/data/bg_426x240.png"
TILE = 16

# The Japanese line is drawn whatever the UI language is: this app is the
# check that the glyphs come out at all. language() only picks the label.
SAMPLE = "こんにちは ABC 日本語"
LABELS = {
    "ja": ["既定フォント", "日本語フォント", "混在"],
    "en": ["default font", "ja font", "mixed"],
}


class PyGfxApp(FmrbApp):
    def on_create(self):
        self.mode = 0
        self.frame = 0
        self.image = None
        self.sprite_x = 20
        self.sprite_y = 60

        self.load_assets()
        self.redraw()

    def on_destroy(self):
        # The canvas takes its sprites with it, but an app that keeps running
        # should still drop what it no longer needs.
        if self.player:
            self.player.destroy()
        for img in self.frames:
            img.destroy()
        if self.sheet:
            self.sheet.destroy()
        if self.badge:
            self.badge.destroy()

    # ---- assets ----

    def load_assets(self):
        self.frames = []
        self.player = None
        self.sheet = None
        self.badge = None

        # The graphics side reads files from its own filesystem, so each asset
        # is copied across first (only when it differs from the copy there).
        names = ["player_down_0.bmp", "player_down_1.bmp", "world.bmp"]
        for name in names:
            self.gfx.sync_file(RPG_DIR + "/" + name, CACHE_DIR + "/" + name)

        # Two 16x16 frames of the walking player, as one sprite.
        for name in ("player_down_0.bmp", "player_down_1.bmp"):
            img = SpriteImage(self.gfx, TILE, TILE, transparent_color=0,
                              use_transparent=True)
            img.load_bmp(CACHE_DIR + "/" + name)
            self.frames.append(img)
        self.player = SpriteInstance(self.gfx, self.frames, self.sprite_x,
                                     self.sprite_y, 1)

        # The tile sheet stays an image: draw_tile stamps rectangles out of it
        # without allocating an instance per tile.
        self.sheet = SpriteImage(self.gfx, 128, 128, transparent_color=0,
                                 use_transparent=False)
        self.sheet.load_bmp(CACHE_DIR + "/world.bmp")

        # An image drawn from Python rather than loaded: a badge for the
        # corner, painted while it is the drawing target.
        self.badge = SpriteImage(self.gfx, 20, 12)
        self.badge.set_target()
        self.gfx.fill_rect(0, 0, 20, 12, FmrbGfx.BLUE)
        self.gfx.draw_rect(0, 0, 20, 12, FmrbGfx.WHITE)
        self.gfx.draw_text(3, 2, "PY", FmrbGfx.WHITE)
        self.badge.reset_target()

    # ---- drawing ----

    def redraw(self):
        g = self.gfx
        self.clear_user_area()

        x0 = self.user_area_x0
        y0 = self.user_area_y0

        # A row of tiles out of the sheet, straight onto the canvas.
        for i in range(6):
            g.draw_tile(self.sheet.id, (i % 4) * TILE, 0, TILE, TILE,
                        x0 + 2 + i * TILE, y0 + 2)

        # The badge, stamped from the image drawn above.
        g.draw_tile(self.badge.id, 0, 0, 20, 12, self.user_area_x1 - 24, y0 + 3)

        self.draw_sample_text(x0 + 3, y0 + TILE + 8)

        g.draw_text(x0 + 3, self.user_area_y1 - 10,
                    "T:font SPACE:frame arrows:move", FmrbGfx.GRAY)

        self.draw_window_frame()
        g.present()

    # The three ways a line can be drawn, so the difference is visible at once.
    def draw_sample_text(self, x, y):
        g = self.gfx
        labels = LABELS.get(language(), LABELS["en"])
        text = SAMPLE

        if self.mode == 0:
            g.set_font(FmrbGfx.FONT_DEFAULT)
            g.draw_text(x, y + 10, "ABC abc 123", FmrbGfx.WHITE)
            width = g.text_width("ABC abc 123")
        elif self.mode == 1:
            g.set_font(FmrbGfx.FONT_JA)
            g.draw_text(x, y + 10, text, FmrbGfx.YELLOW)
            width = g.text_width(text)
            g.set_font(FmrbGfx.FONT_DEFAULT)
        else:
            g.draw_text(x, y + 10, text, FmrbGfx.CYAN, None, True)
            width = g.text_width(text)

        # The label is drawn mixed too, so a Japanese label also has to work.
        g.draw_text(x, y, labels[self.mode] + " (" + language() + ")",
                    FmrbGfx.GRAY, None, True)
        # The measured width is drawn as a line under the text: if text_width
        # disagrees with the renderer, the line ends in the wrong place.
        g.draw_line(x, y + 20, x + width - 1, y + 20, FmrbGfx.RED)
        g.draw_text(x, y + 24, "width " + str(width) + "px bytes " + str(len(text)),
                    FmrbGfx.GRAY)

    # ---- input ----

    def on_event(self, ev):
        super().on_event(ev)
        if ev.get("type") != "key_down":
            return
        sc = ev.get("scancode")
        if sc == 0x17:      # T
            self.mode = (self.mode + 1) % 3
            self.redraw()
        elif sc == 0x0C:    # I
            self.toggle_image()
        elif sc == 0x2C:    # space
            self.frame = 1 - self.frame
            self.player.set_frame(self.frame)
            self.gfx.present()
        elif sc in (0x4F, 0x50, 0x51, 0x52):  # right, left, down, up
            dx = 4 if sc == 0x4F else (-4 if sc == 0x50 else 0)
            dy = 4 if sc == 0x51 else (-4 if sc == 0x52 else 0)
            self.sprite_x += dx
            self.sprite_y += dy
            self.player.move(self.sprite_x, self.sprite_y)
            # A sprite is composited when the canvas is presented, so a move
            # without this shows up only at the next redraw.
            self.gfx.present()

    # create_image is the other way to get pixels on screen: the graphics side
    # decodes the file and hands back an id, and draw_image can scale it.
    # Sprites are for things that move; this is for a picture drawn once.
    def toggle_image(self):
        if self.image:
            self.gfx.delete_image(self.image["id"])
            self.image = None
            self.redraw()
            return

        # A PNG: create_image is the decoder path, and the graphics side keeps
        # the desktop wallpaper on its own filesystem already.
        self.image = self.gfx.create_image(WALLPAPER)
        if not self.image:
            Log.warn("pygfx: create_image failed")
            return
        Log.info("pygfx: image id=" + str(self.image["id"]) + " " +
                 str(self.image["width"]) + "x" + str(self.image["height"]))
        self.gfx.draw_image(self.image["id"], self.user_area_x0 + 2,
                            self.user_area_y0 + 40, 0.5)
        self.gfx.present()

    def on_update(self):
        return 300


app = PyGfxApp()
app.start()

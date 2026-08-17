# ブロック崩し -- the Python sample game.
#
# Everything the Python framework gained in phase5-8 is here: sprites for the
# things that move, tiles for the things that do not, Japanese text, a tune on
# the main sound chip with effects on the sub one, timers for the items, and
# suspend / resume / fullscreen handling.
#
# How it is drawn is the point of the design: the bat, the ball and the falling
# items are sprites (the graphics side composites them, so moving one costs a
# single command), while the bricks and the background are drawn once and only
# repainted where a brick disappears. A frame therefore costs the ball's
# arithmetic plus three commands, not a redraw of the whole field.

import random

CACHE = "/cache/app/breakout"
ASSETS = "/app/game/breakout"

# --- field -----------------------------------------------------------------

COLS = 8
ROWS = 5
BRICK_W = 29
BRICK_H = 8
BRICK_GAP = 1
BALL_R = 4          # the ball sprite is 4x4
PADDLE_H = 6
PADDLE_W = 32
PADDLE_WIDE_W = 48

# Brick colours by row, top row worth the most.
ROW_COLOR = (0xE0, 0xEC, 0xFC, 0x9C, 0x1C)
ROW_SCORE = (50, 40, 30, 20, 10)

# --- feel ------------------------------------------------------------------

# The ball's direction is a unit vector in sixteenths of a pixel: a pair whose
# length is BALL_UNIT. Where the ball lands on the bat picks one of these, so
# the player steers by hitting the ball with the edge or the middle of the bat
# -- the outer zones send it away at a shallow angle, the middle sends it up.
# Sixteenths rather than an angle in degrees because there is no trigonometry
# to do at run time, and integers keep every bounce exactly repeatable.
BALL_UNIT = 16
BALL_ANGLES = (
    (-15, -6),      # far left of the bat: shallow, to the left
    (-13, -10),
    (-9, -13),
    (-3, -16),      # middle: nearly straight up, but never exactly
    (9, -13),
    (13, -10),
    (15, -6),       # far right: shallow, to the right
)

FRAME_MS = 33               # the target: 30 frames a second
PADDLE_SPEED = 6            # pixels per frame while a key is held down
PADDLE_TAP = 3              # what a single press moves, before the hold takes over
# Pixels per frame, before the stage bonus. One more than the old value
# because a substep now covers one pixel of travel rather than one pixel on
# each axis, which used to make a 45 degree ball 1.4 times faster than this
# number said.
BALL_SPEED_BASE = 3
ITEM_FALL = 1
ITEM_CHANCE = 5             # a broken brick drops one with this chance, 1 in N
ITEM_WIDE_MS = 8000
ITEM_SLOW_MS = 6000
START_LIVES = 3

# --- sound -----------------------------------------------------------------

BGM_SLOT = 3
CH_SE = FmrbAudio.CH_PULSE1      # effects; the tune uses pulse2 and triangle
CH_LOW = FmrbAudio.CH_TRIANGLE

# --- colours ---------------------------------------------------------------

C_BG = 0x00
C_TEXT = 0xFF
C_DIM = 0x6D
C_WARN = 0xE0
C_OK = 0x1C
C_KEY = 0xFC

# --- words -----------------------------------------------------------------

WORDS = {
    "ja": {
        "ready": "スペースで はじめる",
        "clear": "ステージクリア!",
        "over": "ゲームオーバー",
        "again": "R で もういちど",
        "score": "とくてん",
        "lives": "のこり",
        "stage": "ステージ",
        "wide": "バーがのびた",
        "slow": "たまがおそく",
        "life": "のこりがふえた",
    },
    "en": {
        "ready": "SPACE to start",
        "clear": "STAGE CLEAR!",
        "over": "GAME OVER",
        "again": "R to play again",
        "score": "SCORE",
        "lives": "LIVES",
        "stage": "STAGE",
        "wide": "wider bat",
        "slow": "slower ball",
        "life": "one more life",
    },
}


class BreakoutApp(FmrbApp):
    def on_create(self):
        self.words = WORDS.get(language(), WORDS["en"])
        self.gfx.set_font(FmrbGfx.FONT_JA, 12)
        self.audio = FmrbAudio(self)
        self.note_off_at = {}
        # Which way the bat is being driven. A press moves it one step and sets
        # the flag; the frame then keeps moving it while the key is down. Both
        # halves are needed: waiting for the keyboard's own repeat makes the
        # bat lurch, and moving only on the press makes a held key do nothing.
        self.move_left = False
        self.move_right = False
        self.paused = False
        self.frame_worst = 0

        self.load_assets()
        self.start_game()

    def on_destroy(self):
        self.silence()
        self.audio.stop()
        Log.info("breakout: worst frame " + str(self.frame_worst) + " ms")

    # ---- assets ----

    def load_assets(self):
        names = ("paddle.bmp", "paddle_wide.bmp", "ball.bmp",
                 "item_wide.bmp", "item_slow.bmp", "item_life.bmp", "bgm.fmsq")
        for name in names:
            self.gfx.sync_file(ASSETS + "/" + name, CACHE + "/" + name)

        self.img_paddle = self.load_sprite("paddle.bmp", PADDLE_W, PADDLE_H)
        self.img_paddle_wide = self.load_sprite("paddle_wide.bmp", PADDLE_WIDE_W,
                                                PADDLE_H)
        self.img_ball = self.load_sprite("ball.bmp", BALL_R, BALL_R)
        # One image per item kind, all frames of a single sprite: a falling
        # item changes what it is by changing frame, not by being recreated.
        self.item_images = [self.load_sprite("item_wide.bmp", 8, 8),
                            self.load_sprite("item_slow.bmp", 8, 8),
                            self.load_sprite("item_life.bmp", 8, 8)]

        self.paddle = SpriteInstance(self.gfx, [self.img_paddle,
                                                self.img_paddle_wide], 0, 0, 2)
        self.ball = SpriteInstance(self.gfx, self.img_ball, 0, 0, 3)
        self.item = SpriteInstance(self.gfx, self.item_images, 0, 0, 1)
        self.item.set_visible(False)

        try:
            self.audio.load_fmsq_file(BGM_SLOT, CACHE + "/bgm.fmsq")
        except Exception as e:
            Log.warn("breakout: no bgm (" + str(e) + ")")

    def load_sprite(self, name, w, h):
        img = SpriteImage(self.gfx, w, h, transparent_color=0, use_transparent=True)
        img.load_bmp(CACHE + "/" + name)
        return img

    # ---- geometry ----
    #
    # Recomputed rather than stored as constants, because the window can be
    # resized (and a fullscreen switch is a resize).
    def measure(self):
        self.fx0 = self.user_area_x0 + 1
        self.fx1 = self.user_area_x1 - 1
        self.status_y = self.user_area_y0 + 1
        self.ftop = self.user_area_y0 + 14
        self.fbottom = self.user_area_y1 - 2
        self.paddle_y = self.fbottom - PADDLE_H - 2
        self.brick_x0 = self.fx0 + ((self.fx1 - self.fx0) -
                                    COLS * (BRICK_W + BRICK_GAP)) // 2
        self.brick_y0 = self.ftop + 6

    # ---- game state ----

    def start_game(self):
        self.score = 0
        self.lives = START_LIVES
        self.stage = 1
        self.state = "ready"
        self.message = self.words["ready"]
        self.build_stage()
        if self.audio:
            self.audio.play_slot(BGM_SLOT, FmrbAudio.MAIN)

    def build_stage(self):
        self.measure()
        # One row per list, one entry per column: True while the brick stands.
        self.bricks = [[True] * COLS for _ in range(ROWS)]
        self.left = COLS * ROWS
        self.paddle_w = PADDLE_W
        self.paddle.set_frame(0)
        self.paddle_x = (self.fx0 + self.fx1) // 2 - self.paddle_w // 2
        self.move_left = False
        self.move_right = False
        self.wide_until = 0
        self.slow_until = 0
        self.item_kind = -1
        self.item.set_visible(False)
        self.reset_ball()
        self.draw_all()

    def reset_ball(self):
        self.ball.set_visible(True)
        self.ball_x = self.paddle_x + self.paddle_w // 2 - BALL_R // 2
        self.ball_y = self.paddle_y - BALL_R - 1
        self.sync_ball_fixed()
        # Launched a little off the vertical, so the first bounce already has
        # somewhere to go.
        self.ball_ux, self.ball_uy = BALL_ANGLES[4]
        self.stuck = True       # riding the bat until the player launches it

    # The pixel position is what everything else reads; this is where the
    # sixteenths are re-anchored to it (after a reset, a resize or a bounce
    # that clamps the ball back inside the field).
    def sync_ball_fixed(self):
        self.bx16 = self.ball_x * BALL_UNIT
        self.by16 = self.ball_y * BALL_UNIT

    def ball_speed(self):
        speed = BALL_SPEED_BASE + (self.stage - 1) // 2
        if self.slow_until and ticks_ms() < self.slow_until:
            speed -= 1
        return speed if speed >= 1 else 1

    # ---- drawing ----

    def draw_all(self):
        self.clear_user_area(C_BG)
        self.draw_status()
        self.draw_bricks()
        self.draw_message()
        self.place_sprites()
        self.draw_window_frame()
        self.gfx.present()

    def draw_status(self):
        g = self.gfx
        y = self.status_y
        g.fill_rect(self.fx0, y, self.fx1 - self.fx0, 12, C_BG)
        g.draw_text(self.fx0, y, self.words["score"] + " " + str(self.score),
                    C_TEXT, None, True)
        mid = (self.fx0 + self.fx1) // 2 - 20
        g.draw_text(mid, y, self.words["lives"] + " " + str(self.lives),
                    C_OK if self.lives > 1 else C_WARN, None, True)
        right = self.fx1 - 62
        g.draw_text(right, y, self.words["stage"] + " " + str(self.stage),
                    C_KEY, None, True)

    def draw_bricks(self):
        g = self.gfx
        for row in range(ROWS):
            for col in range(COLS):
                if self.bricks[row][col]:
                    self.draw_brick(g, row, col, ROW_COLOR[row])

    def draw_brick(self, g, row, col, colour):
        x = self.brick_x0 + col * (BRICK_W + BRICK_GAP)
        y = self.brick_y0 + row * (BRICK_H + BRICK_GAP)
        g.fill_rect(x, y, BRICK_W, BRICK_H, colour)
        # A lit top edge and a dark bottom one: the wall reads as bricks
        # rather than as one block of colour.
        g.draw_line(x, y, x + BRICK_W - 1, y, C_TEXT)

    # Only the brick that broke is repainted -- that is what keeps a frame
    # cheap. Repainting the whole wall costs one command per brick.
    def erase_brick(self, row, col):
        x = self.brick_x0 + col * (BRICK_W + BRICK_GAP)
        y = self.brick_y0 + row * (BRICK_H + BRICK_GAP)
        self.gfx.fill_rect(x, y, BRICK_W, BRICK_H, C_BG)

    def draw_message(self):
        g = self.gfx
        y = (self.ftop + self.fbottom) // 2
        g.fill_rect(self.fx0, y - 2, self.fx1 - self.fx0, 30, C_BG)
        if not self.message:
            return
        w = g.text_width(self.message)
        x = (self.fx0 + self.fx1) // 2 - w // 2
        colour = C_WARN if self.state == "over" else C_OK
        g.draw_text(x, y, self.message, colour, None, True)
        if self.state == "over" or self.state == "clear":
            sub = self.words["again"] if self.state == "over" else ""
            if sub:
                sw = g.text_width(sub)
                g.draw_text((self.fx0 + self.fx1) // 2 - sw // 2, y + 14, sub,
                            C_DIM, None, True)

    def place_sprites(self):
        self.paddle.move(self.paddle_x, self.paddle_y)
        self.ball.move(self.ball_x, self.ball_y)

    # ---- the frame ----

    def on_update(self):
        started = ticks_ms()
        self.tick_notes()
        self.tick_items_effects()

        if self.state == "play" or self.state == "ready":
            self.step_paddle()
            self.step_ball()
            self.step_item()
            self.place_sprites()
            self.gfx.present()

        spent = ticks_ms() - started
        if spent > self.frame_worst:
            self.frame_worst = spent
        # Give back what is left of the frame, and never less than a tick.
        rest = FRAME_MS - spent
        return rest if rest > 1 else 1

    def step_paddle(self):
        if self.move_left == self.move_right:
            return          # neither, or both: stand still
        step = -PADDLE_SPEED if self.move_left else PADDLE_SPEED
        self.set_paddle_x(self.paddle_x + step)

    def step_ball(self):
        if self.stuck:
            self.ball_x = self.paddle_x + self.paddle_w // 2 - BALL_R // 2
            self.ball_y = self.paddle_y - BALL_R - 1
            self.sync_ball_fixed()
            return

        # The direction vector is one pixel long, so one substep moves the ball
        # at most one pixel and it can never pass through a brick. The speed is
        # how many substeps a frame takes, which is why a faster ball costs
        # more arithmetic but never skips a collision.
        steps = self.ball_speed()
        i = 0
        while i < steps:
            i += 1
            self.bx16 += self.ball_ux
            self.by16 += self.ball_uy
            self.ball_x = self.bx16 // BALL_UNIT
            self.ball_y = self.by16 // BALL_UNIT

            # The direction is part of the test, not just the position: a
            # substep is a fraction of a pixel, so the ball sits on the wall's
            # pixel for several of them. Testing the position alone flips the
            # direction again on each of those and the ball never leaves.
            if self.ball_x <= self.fx0 and self.ball_ux < 0:
                self.ball_x = self.fx0
                self.ball_ux = -self.ball_ux
                self.sync_ball_fixed()
                self.se_wall()
            elif self.ball_x + BALL_R >= self.fx1 and self.ball_ux > 0:
                self.ball_x = self.fx1 - BALL_R
                self.ball_ux = -self.ball_ux
                self.sync_ball_fixed()
                self.se_wall()

            if self.ball_y <= self.ftop and self.ball_uy < 0:
                self.ball_y = self.ftop
                self.ball_uy = -self.ball_uy
                self.sync_ball_fixed()
                self.se_wall()

            if self.hit_brick():
                continue

            if self.hit_paddle():
                continue

            if self.ball_y > self.fbottom:
                self.lose_life()
                return

    # The ball's position gives the row and column directly, so only the one
    # brick under it is looked at -- no walk over the wall.
    def hit_brick(self):
        if self.ball_uy > 0 and self.ball_y < self.brick_y0:
            return False
        col = (self.ball_x + BALL_R // 2 - self.brick_x0) // (BRICK_W + BRICK_GAP)
        row = (self.ball_y + BALL_R // 2 - self.brick_y0) // (BRICK_H + BRICK_GAP)
        if col < 0 or col >= COLS or row < 0 or row >= ROWS:
            return False
        if not self.bricks[row][col]:
            return False

        self.bricks[row][col] = False
        self.left -= 1
        self.score += ROW_SCORE[row]
        self.erase_brick(row, col)
        self.draw_status()
        # Straight back the way it came, vertically: a brick is wider than it
        # is tall, so that is what it looks like nine times out of ten.
        self.ball_uy = -self.ball_uy
        self.se_brick(row)
        self.maybe_drop_item(row, col)

        if self.left == 0:
            self.stage_cleared()
        return True

    def hit_paddle(self):
        if self.ball_uy < 0:
            return False
        if self.ball_y + BALL_R < self.paddle_y:
            return False
        if self.ball_y > self.paddle_y + PADDLE_H:
            return False
        if (self.ball_x + BALL_R < self.paddle_x or
                self.ball_x > self.paddle_x + self.paddle_w):
            return False

        self.ball_y = self.paddle_y - BALL_R
        # Where on the bat it landed decides the angle. The bat is divided into
        # as many zones as there are angles: the middle sends the ball back up,
        # the edges send it off to the side. This is the whole of the player's
        # aim -- the ball does not remember the angle it arrived at.
        rel = self.ball_x + BALL_R // 2 - self.paddle_x
        zone = rel * len(BALL_ANGLES) // self.paddle_w
        if zone < 0:
            zone = 0
        elif zone >= len(BALL_ANGLES):
            zone = len(BALL_ANGLES) - 1
        self.ball_ux, self.ball_uy = BALL_ANGLES[zone]
        self.sync_ball_fixed()
        self.se_paddle()
        return True

    def lose_life(self):
        self.lives -= 1
        self.se_miss()
        self.draw_status()
        if self.lives <= 0:
            self.state = "over"
            self.message = self.words["over"]
            # Nothing is moving any more, so nothing should be left lying on
            # the field: the ball and any item in mid-air go away with it.
            self.ball.set_visible(False)
            self.drop_item()
            self.audio.stop()
            self.draw_message()
            self.gfx.present()
            return
        self.state = "ready"
        self.message = self.words["ready"]
        self.reset_ball()
        self.draw_message()
        self.gfx.present()

    def stage_cleared(self):
        self.state = "clear"
        self.message = self.words["clear"]
        self.stuck = True
        self.se_clear()
        self.draw_message()
        self.gfx.present()
        # Next stage after a moment, so the sound and the words land first.
        self.set_timer(1800, self.next_stage)

    def next_stage(self):
        self.stage += 1
        self.state = "ready"
        self.message = self.words["ready"]
        self.build_stage()

    # ---- items ----

    def maybe_drop_item(self, row, col):
        if self.item_kind >= 0:
            return          # only one on the way down at a time
        if random.randint(1, ITEM_CHANCE) != 1:
            return
        self.item_kind = random.randint(0, 2)
        self.item_x = self.brick_x0 + col * (BRICK_W + BRICK_GAP) + BRICK_W // 2 - 4
        self.item_y = self.brick_y0 + row * (BRICK_H + BRICK_GAP)
        self.item.set_frame(self.item_kind)
        self.item.move(self.item_x, self.item_y)
        self.item.set_visible(True)

    def step_item(self):
        if self.item_kind < 0:
            return
        self.item_y += ITEM_FALL
        if (self.item_y + 8 >= self.paddle_y and
                self.item_y <= self.paddle_y + PADDLE_H and
                self.item_x + 8 >= self.paddle_x and
                self.item_x <= self.paddle_x + self.paddle_w):
            self.take_item(self.item_kind)
            self.drop_item()
            return
        if self.item_y > self.fbottom:
            self.drop_item()
            return
        self.item.move(self.item_x, self.item_y)

    def drop_item(self):
        self.item_kind = -1
        self.item.set_visible(False)

    def take_item(self, kind):
        now = ticks_ms()
        if kind == 0:
            self.paddle_w = PADDLE_WIDE_W
            self.paddle.set_frame(1)
            self.wide_until = now + ITEM_WIDE_MS
            self.message = self.words["wide"]
        elif kind == 1:
            self.slow_until = now + ITEM_SLOW_MS
            self.message = self.words["slow"]
        else:
            self.lives += 1
            self.message = self.words["life"]
            self.draw_status()
        self.se_item()
        self.draw_message()
        # The pickup line is only worth a moment; a timer clears it, which is
        # why timers had to keep running inside the wait.
        self.set_timer(1200, self.clear_message)

    def clear_message(self):
        if self.state == "play":
            self.message = ""
            self.draw_message()

    # Effects that run out on the clock rather than on a frame count.
    def tick_items_effects(self):
        if self.wide_until and ticks_ms() >= self.wide_until:
            self.wide_until = 0
            self.paddle_w = PADDLE_W
            self.paddle.set_frame(0)
        if self.slow_until and ticks_ms() >= self.slow_until:
            self.slow_until = 0

    # ---- sound ----
    #
    # Effects go to the sub sound chip through note_on/note_off, so the tune
    # on the main one is never interrupted. Each note books the time it must
    # stop at: a slow frame then shortens the gap, it does not stretch the note.

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
                if due is None:
                    due = [ch]
                else:
                    due.append(ch)
        if due is None:
            return
        for ch in due:
            self.audio.note_off(ch)
            del self.note_off_at[ch]

    def silence(self):
        for ch in list(self.note_off_at):
            self.audio.note_off(ch)
        self.note_off_at = {}

    def se_wall(self):
        self.note(CH_SE, 440, 25, 8)

    def se_paddle(self):
        self.note(CH_SE, 660, 40, 12)

    def se_brick(self, row):
        # Higher rows ring higher, so the wall sounds like it is being eaten
        # from the bottom up.
        self.note(CH_SE, 700 + (ROWS - row) * 120, 45, 12)

    def se_item(self):
        self.note(CH_SE, 1047, 90, 14)

    def se_miss(self):
        self.note(CH_LOW, 98, 400, 0, 0)

    def se_clear(self):
        self.note(CH_SE, 784, 300, 14)

    # ---- input ----

    def on_event(self, ev):
        super().on_event(ev)
        kind = ev.get("type")
        if kind == "mouse_move":
            # The bat follows the pointer, which is the natural way to play
            # this on a machine with a mouse.
            self.set_paddle_x(ev.get("x", 0) - self.paddle_w // 2)
            return

        sc = ev.get("scancode")
        if kind == "key_up":
            # Only the direction keys are tracked; a release of anything else
            # is nothing to do with the bat.
            if sc == 0x50 or sc == 0x04:
                self.move_left = False
            elif sc == 0x4F or sc == 0x07:
                self.move_right = False
            return
        if kind != "key_down":
            return

        if sc == 0x50 or sc == 0x04:        # left, A
            self.move_left = True
            self.set_paddle_x(self.paddle_x - PADDLE_TAP)
        elif sc == 0x4F or sc == 0x07:      # right, D
            self.move_right = True
            self.set_paddle_x(self.paddle_x + PADDLE_TAP)
        elif sc == 0x2C:                    # space
            self.launch()
        elif sc == 0x15:                    # R
            if self.state == "over":
                self.start_game()
        elif sc == 0x09:                    # F
            self.toggle_fullscreen()

    # The bat is a sprite, so moving it is one command; the frame presents it
    # along with the ball, and nothing is redrawn.
    def set_paddle_x(self, x):
        if x < self.fx0:
            x = self.fx0
        if x + self.paddle_w > self.fx1:
            x = self.fx1 - self.paddle_w
        # No present here: the frame does that for the bat and the ball
        # together, at most 33 ms away.
        self.paddle_x = x

    def launch(self):
        if self.state != "ready":
            return
        self.state = "play"
        self.message = ""
        self.stuck = False
        self.draw_message()
        self.gfx.present()

    # ---- the rest of the lifecycle ----

    def on_suspend(self):
        # Another app took the screen: stop the ball and the sound, and leave
        # the game exactly where it was.
        self.paused = self.state == "play"
        if self.paused:
            self.state = "ready"
            self.message = self.words["ready"]
            self.stuck = True
        self.silence()
        self.audio.stop()

    def on_resume(self):
        self.audio.play_slot(BGM_SLOT, FmrbAudio.MAIN)
        self.draw_all()

    def on_resize(self, width, height):
        # The field is measured from the user area, so a resize rebuilds it.
        # The score and the stage survive; the wall is redrawn where it fits.
        self.measure()
        self.paddle_x = (self.fx0 + self.fx1) // 2 - self.paddle_w // 2
        self.reset_ball()
        self.state = "ready"
        self.message = self.words["ready"]
        self.draw_all()


app = BreakoutApp()
app.start()

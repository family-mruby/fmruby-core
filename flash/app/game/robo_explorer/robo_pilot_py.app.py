# Robot Explorer -- the pilot frame, in Python.
#
# The world (robo_explorer.app.rb) is Ruby and is not touched: this app talks
# to it over the same three topics the Ruby pilot uses. Two apps written in
# two languages, one conversation.
#
# The part a player writes lives in my_pilot.py, imported below. This file is
# the plumbing around it -- subscribe, draw, send.

# The brain is the player's file, so it is the one thing here that can be
# broken. A bad import must leave the frame standing and say why on screen,
# not take the app down before it has drawn anything.
try:
    import my_pilot
    BRAIN_ERROR = None
except Exception as e:
    my_pilot = None
    BRAIN_ERROR = str(e)

TOPIC_CMD = "robo/cmd"
TOPIC_STATE = "robo/state"
TOPIC_RESULT = "robo/result"

COL_TEXT = 0xFF   # white
COL_KEY = 0xFC    # yellow
COL_GOAL = 0x1C   # green
COL_NG = 0xE0     # red
COL_DIM = 0x6D    # gray
COL_BG = 0x00
COL_WIRE = 0xFF
COL_DOOR = 0xA8
COL_KEYM = 0xFC

LINE_H = 13
RTT_MAX = 10      # how many command round trips to log

# First-person view box, same geometry as the Ruby pilot so the two can be
# compared side by side. Frames are inclusive corners, one per corridor depth.
VB_X = 3
VB_Y = 3
VB_W = 150
VB_H = 76
VB_FRAMES = [
    [1, 1, 148, 74],
    [26, 13, 123, 62],
    [43, 22, 106, 53],
    [54, 28, 95, 47],
    [62, 31, 87, 44],
    [66, 34, 83, 41],
]

THINK_PRESETS = [500, 200, 100]

KEY_S = 0x16
KEY_1 = 0x1E
KEY_3 = 0x20

DIR_JA = {"N": "北", "E": "東", "S": "南", "W": "西",
          "NE": "北東", "NW": "北西", "SE": "南東", "SW": "南西"}
FRONT_JA = {"wall": "壁", "key": "鍵", "door": "扉", "goal": "ゴール"}
OK_JA = {"move": "進んだ", "turn": "回った", "reset": "リセットした"}
NG_JA = {"wall": "前は壁", "locked": "鍵がない", "edge": "外には出られない",
         "done": "ゴール済み"}


class RoboPilotPyApp(FmrbApp):
    def on_create(self):
        self.gfx.set_font(FmrbGfx.FONT_JA, 12)
        self.state = None
        self.result_text = ""
        self.result_ok = True
        self.sent_at = None
        self.rtt_n = 0
        self.last_send = 0
        self.dirty = False
        # Armed, not running: S starts the autopilot, so the run begins when
        # the player decides it does.
        self.auto = False
        self.think_ms = THINK_PRESETS[1]
        self.brain = self.new_brain()

        self.subscribe(TOPIC_STATE)
        self.subscribe(TOPIC_RESULT)
        self.draw_screen()
        Log.info("RoboPilotPy: ready")

    def on_destroy(self):
        self.unsubscribe(TOPIC_STATE)
        self.unsubscribe(TOPIC_RESULT)

    # ---- receiving ----

    def on_control(self, msg):
        if msg.get("cmd") != "topic_data":
            return
        data = msg.get("data")
        if not data:
            return
        topic = msg.get("topic")
        if topic == TOPIC_STATE:
            # Turn 0 means the world restarted. The answer to the last command
            # belongs to the run that just ended, and so does what the brain
            # remembers: a new world gets a new brain.
            if data.get("turn") == 0:
                self.result_text = ""
                self.brain = self.new_brain()
            self.state = data
            self.dirty = True
        elif topic == TOPIC_RESULT:
            self.note_result(data)
            self.dirty = True

    def note_result(self, data):
        self.result_ok = bool(data.get("ok"))
        self.result_text = self.result_ja(data.get("op"), self.result_ok,
                                          data.get("reason"))
        if self.sent_at is not None and self.rtt_n < RTT_MAX:
            self.rtt_n += 1
            Log.info("RoboPilotPy: rtt " + str(self.rtt_n) + " = " +
                     str(ticks_ms() - self.sent_at) + " ms")
        self.sent_at = None
        self.call_brain(lambda: self.brain.on_result(data, self.state))

    def result_ja(self, op, ok, reason):
        if ok:
            return OK_JA.get(op, "待った")
        return NG_JA.get(reason, "不明な命令")

    # ---- the brain ----

    # A broken MyPilot must not take the frame down with it: the error goes to
    # the panel and the log, and the world keeps running, so a fixed brain can
    # be launched into the same run.
    def new_brain(self):
        if my_pilot is None:
            return None
        try:
            return my_pilot.MyPilot()
        except Exception as e:
            Log.error("MyPilot: " + str(e))
            return None

    def call_brain(self, fn):
        if self.brain is None:
            return None
        try:
            return fn()
        except Exception as e:
            self.result_ok = False
            self.result_text = "頭脳でエラー"
            Log.error("MyPilot: " + str(e))
            return None

    def send_cmd(self, cmd):
        if not cmd:
            return
        self.sent_at = ticks_ms()
        self.last_send = self.sent_at
        self.publish(TOPIC_CMD, cmd)

    def on_event(self, ev):
        super().on_event(ev)
        if ev.get("type") != "key_down":
            return
        sc = ev.get("scancode")
        # S and 1-3 belong to the frame, not the brain: every brain gets the
        # same start/stop and the same choice of pace.
        if sc == KEY_S:
            self.auto = not self.auto
            self.dirty = True
            return
        if KEY_1 <= sc <= KEY_3:
            self.think_ms = THINK_PRESETS[sc - KEY_1]
            self.dirty = True
            return
        if self.state is None:
            return
        self.send_cmd(self.call_brain(lambda: self.brain.on_key(sc, self.state)))

    # think() runs on the update tick rather than on every arriving message:
    # steady pacing, and no feedback loop through the broker. Drawing happens
    # here too, once per tick at most -- redrawing the wireframe per message
    # cannot keep up with the message rate, and the brain would then be fed
    # states that are seconds old.
    def on_update(self):
        now = ticks_ms()
        if (self.auto and self.state and not self.state.get("done")
                and now - self.last_send >= self.think_ms):
            self.send_cmd(self.call_brain(lambda: self.brain.think(self.state)))
        if self.dirty:
            self.dirty = False
            self.draw_screen()
        return self.think_ms

    # ---- drawing ----

    def draw_screen(self):
        g = self.gfx
        self.clear_user_area()
        x = self.user_area_x0 + 3
        if self.brain is None:
            g.draw_text(x, self.user_area_y0 + 3, "頭脳が読めない", COL_NG)
            g.draw_text(x, self.user_area_y0 + 3 + LINE_H,
                        BRAIN_ERROR if BRAIN_ERROR else "MyPilot が作れない",
                        COL_DIM)
            g.draw_text(x, self.user_area_y0 + 3 + LINE_H * 2,
                        "my_pilot.py を直して再起動", COL_DIM)
            self.draw_window_frame()
            g.present()
            return

        st = self.state
        if st is None:
            g.draw_text(x, self.user_area_y0 + 3, "待機中", COL_DIM)
            self.draw_window_frame()
            g.present()
            return

        self.draw_view(st)

        y = self.user_area_y0 + VB_Y + VB_H + 4
        g.draw_text(x, y, "場所 " + str(st["x"]) + "," + str(st["y"]) + "  " +
                    self.dir_ja(st["dir"]), COL_TEXT)
        y += LINE_H
        g.draw_text(x, y, "前 " + self.front_ja(st["front"]), COL_TEXT)
        y += LINE_H
        g.draw_text(x, y, "鍵 " + str(st["keys"]) + "  ゴール " +
                    self.dir_ja(st["goal"]), COL_KEY)
        y += LINE_H
        g.draw_text(x, y, "手数 " + str(st["steps"]), COL_TEXT)
        y += LINE_H
        if self.result_text:
            g.draw_text(x, y, self.result_text,
                        COL_GOAL if self.result_ok else COL_NG)

        y = self.user_area_y1 - LINE_H * 2 - 4
        g.draw_text(x, y, ("自動:ON" if self.auto else "自動:OFF") +
                    "  速度:" + str(self.think_ms) + "ms",
                    COL_GOAL if self.auto else COL_DIM)
        y += LINE_H
        g.draw_text(x, y, "S:自動 1-3:速度 矢印:手動", COL_DIM)

        self.draw_window_frame()
        g.present()

    def dir_ja(self, code):
        return DIR_JA.get(code, "ここ")

    def front_ja(self, code):
        return FRONT_JA.get(code, "床")

    # ---- first-person view ----
    #
    # One [left_wall, right_wall, kind] per corridor cell, as the world sends
    # it. Nested frames give the depth; each cell draws its two sides between
    # its frame and the next.
    def draw_view(self, st):
        g = self.gfx
        bx = self.user_area_x0 + VB_X
        by = self.user_area_y0 + VB_Y
        g.fill_rect(bx, by, VB_W, VB_H, COL_BG)
        g.draw_rect(bx, by, VB_W, VB_H, COL_DIM)

        view = st.get("view")
        if not view:
            g.draw_text(bx + 8, by + VB_H // 2 - 6, "(視界なし)", COL_DIM)
            return

        # Cleared: the view has nothing left to say, so it becomes the banner.
        if st.get("done"):
            f = VB_FRAMES[0]
            g.draw_rect(bx + f[0], by + f[1], f[2] - f[0] + 1, f[3] - f[1] + 1,
                        COL_GOAL)
            g.draw_text(bx + VB_W // 2 - 21, by + VB_H // 2 - 6, "クリア!", COL_GOAL)
            return

        ended = None
        n = len(view)
        if n > len(VB_FRAMES) - 1:
            n = len(VB_FRAMES) - 1
        i = 0
        while i < n:
            cell = view[i]
            kind = cell[2]
            nf = VB_FRAMES[i]
            ff = VB_FRAMES[i + 1]
            # A door or the goal fills the near plane of its cell: sight stops
            # there, so nothing of that cell is drawn, its side walls included.
            if kind == "door" or kind == "goal":
                ended = kind
                self.draw_view_face(bx, by, nf, kind)
                break
            self.draw_view_side(bx, by, nf, ff, cell[0], True)
            self.draw_view_side(bx, by, nf, ff, cell[1], False)
            # Where a side flips between wall and opening, the edge between the
            # two cells is a vertical line at the boundary frame.
            if i + 1 < n:
                nxt = view[i + 1]
                if nxt[2] != "door" and nxt[2] != "goal":
                    if cell[0] != nxt[0]:
                        self.draw_view_corner(bx, by, ff, True)
                    if cell[1] != nxt[1]:
                        self.draw_view_corner(bx, by, ff, False)
            if kind == "key":
                mx = bx + (nf[0] + nf[2]) // 2 - 3
                my = by + (nf[3] + ff[3]) // 2 - 1
                g.fill_rect(mx, my, 6, 3, COL_KEYM)
            i += 1
        # Ran out of corridor without a door or the goal: a wall faces the
        # robot at the last frame.
        if ended is None:
            self.draw_view_face(bx, by, VB_FRAMES[i], "wall")

    def draw_view_corner(self, bx, by, f, left):
        x = bx + (f[0] if left else f[2])
        self.gfx.draw_line(x, by + f[1], x, by + f[3], COL_WIRE)

    # wall==1 draws the slanted wall; wall==0 draws an opening (near vertical
    # edge plus the ceiling and floor shelves of the side passage).
    def draw_view_side(self, bx, by, nf, ff, wall, left):
        g = self.gfx
        if left:
            nx = bx + nf[0]
            fx = bx + ff[0]
        else:
            nx = bx + nf[2]
            fx = bx + ff[2]
        nty = by + nf[1]
        nby = by + nf[3]
        fty = by + ff[1]
        fby = by + ff[3]
        if wall == 1:
            g.draw_line(nx, nty, fx, fty, COL_WIRE)
            g.draw_line(nx, nby, fx, fby, COL_WIRE)
        else:
            g.draw_line(nx, fty, nx, fby, COL_WIRE)
            g.draw_line(nx, fty, fx, fty, COL_WIRE)
            g.draw_line(nx, fby, fx, fby, COL_WIRE)

    # The facing surface at a frame: a rectangle for a wall, with a leaf inside
    # it for a door, green with a mark for the goal.
    def draw_view_face(self, bx, by, f, kind):
        g = self.gfx
        fx = bx + f[0]
        fy = by + f[1]
        fw = f[2] - f[0] + 1
        fh = f[3] - f[1] + 1
        g.draw_rect(fx, fy, fw, fh, COL_WIRE)
        if kind == "door":
            dw = fw * 2 // 5
            dh = fh * 7 // 10
            dx = fx + (fw - dw) // 2
            dy = fy + fh - dh
            g.draw_rect(dx, dy, dw, dh, COL_DOOR)
            g.fill_rect(dx + dw - 3, dy + dh // 2, 2, 2, COL_DOOR)
        elif kind == "goal":
            g.draw_rect(fx + 2, fy + 2, fw - 4, fh - 4, COL_GOAL)
            if fh >= 16:
                g.draw_text(fx + fw // 2 - 3, fy + fh // 2 - 6, "G", COL_GOAL)


try:
    app = RoboPilotPyApp()
    app.start()
except Exception as e:
    Log.error("RoboPilotPy: " + str(e))

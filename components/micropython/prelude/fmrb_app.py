# FmrbApp - Family mruby OS Python app framework.
#
# Transcribed from the Ruby version (picoruby-fmrb-app mrblib/fmrb-app.rb) so a
# Python app is written the same way a Ruby one is: subclass, override the
# lifecycle methods, call start(). Attribute names match the Ruby instance
# variables (self.gfx, self.window_width, self.user_area_x0, ...).
#
# Deliberately absent rather than stubbed, so calling one raises AttributeError
# instead of quietly doing nothing: extra canvases, scrollbars and GfxBlock.

# fmrb_gfx.py is concatenated ahead of this file (see the order in the
# micropython:prelude rake task) and both run in the app's global namespace, so
# FmrbGfx is already defined here. There is no filesystem importer to import it
# from, which is also why the two files are not modules.

import _fmrb


# Milliseconds since boot. The guest has no time module (it lives in extmod/,
# which the embed package does not carry), so this is the only clock an app
# has: timers, animation and "how long ago" all measure with it.
def ticks_ms():
    return _fmrb.ticks_ms()


# UI language the user chose, "ja" or "en". There is no shared string table:
# an app that wants both keeps its own dict and picks with this.
def language():
    return _fmrb.language()


class Log:
    @staticmethod
    def debug(msg):
        _fmrb.log("D", str(msg))

    @staticmethod
    def info(msg):
        _fmrb.log("I", str(msg))

    @staticmethod
    def warn(msg):
        _fmrb.log("W", str(msg))

    @staticmethod
    def error(msg):
        _fmrb.log("E", str(msg))


# The system theme, read once here so the constants below can be plain class
# attributes -- the same shape Ruby apps get through FmrbConst::THEME_*.
_theme = _fmrb.theme()


class FmrbConst:
    PROC_ID_KERNEL = 0
    MSG_TYPE_APP_CONTROL = 0
    MSG_TYPE_APP_AUDIO = 2
    MSG_TYPE_HID_EVENT = 3

    # [theme] in system_conf.toml, as RGB332 bytes. One edit to that file
    # restyles the desktop and every app together, so an app that shows text
    # and controls should take its colours from here rather than pick its own.
    THEME_DESKTOP_BG = _theme["desktop_bg"]
    THEME_MENU_BG = _theme["menu_bg"]
    THEME_WINDOW_BG = _theme["window_bg"]
    THEME_TEXT = _theme["text"]
    THEME_TEXT_LIGHT = _theme["text_light"]
    THEME_HIGHLIGHT = _theme["highlight"]
    THEME_BORDER = _theme["border"]
    THEME_BUTTON = _theme["button"]
    THEME_DIR_COLOR = _theme["dir_color"]


# Audio, transcribed from the Ruby version (picoruby-fmrb-app
# mrblib/fmrb-audio.rb). Everything here except the notes is one message to the
# kernel, which is why this class needs nothing but an app to send through.
#
# Two APU instances play at once: MAIN (0) carries a tune, SUB (1) carries the
# effects that note_on/note_off make. Keeping a long piece of music on MAIN and
# the short noises on SUB is what stops an effect from cutting the music.
class FmrbAudio:
    MAIN = 0
    SUB = 1

    # Channels, as the APU numbers them.
    CH_PULSE1 = 0
    CH_PULSE2 = 1
    CH_TRIANGLE = 2
    CH_NOISE = 3

    def __init__(self, app):
        self.app = app

    def _send(self, data):
        return self.app.send_message(FmrbConst.PROC_ID_KERNEL,
                                     FmrbConst.MSG_TYPE_APP_AUDIO, data)

    # ---- music ----

    def play(self, path, track=0):
        return self._send({"cmd": "play", "path": path, "track": track})

    def stop(self):
        return self._send({"cmd": "stop"})

    def pause(self):
        return self._send({"cmd": "pause"})

    def resume(self):
        return self._send({"cmd": "resume"})

    # Load a tune into a slot from a file the graphics-audio side can read
    # (push it there with FmrbGfx#sync_file first). The inline form of this
    # would have to fit in one message, so a real tune goes by path.
    def load_fmsq_file(self, slot_id, path):
        return self._send({"cmd": "load_fmsq_file", "slot": slot_id, "path": path})

    def play_slot(self, slot_id, instance=MAIN):
        return self._send({"cmd": "play_slot", "slot": slot_id, "instance": instance})

    # ---- notes ----
    #
    # These two are the only audio calls made in a stream, so they skip the
    # dict and go straight to C: a note allocates nothing at all. The Ruby
    # version does the same thing for the same reason -- a collection in the
    # middle of a tune stops the app long enough to hear.

    def note_on(self, channel, freq, volume=10, duty=2, sweep=0):
        return _fmrb.audio_note(True, channel, freq, volume, duty, sweep)

    def note_off(self, channel):
        return _fmrb.audio_note(False, channel, 0, 0, 0, 0)


class FmrbApp:
    TITLE_BAR_H = 11
    CORNER_R = 4
    TRANSPARENT_COLOR = 0x01

    # Close-button geometry, used by both the frame drawing and the hit test.
    CLOSE_BTN_CX_OFFSET = 6
    CLOSE_BTN_CY = 5
    CLOSE_BTN_R = 3
    CLOSE_BTN_HIT_R = 5
    # The frame's colours are the theme's, not this file's. They used to be
    # spelled out (0xC5, 0xFB, 0x60), which is the light theme written down,
    # and that is why a Python window kept that look after the theme changed.
    CLOSE_BTN_NORMAL_COLOR = FmrbConst.THEME_TEXT_LIGHT
    CLOSE_BTN_PRESSED_COLOR = 0x49   # dark grey: reads as "pressed" on any bar

    TITLE_BAR_COLOR = FmrbConst.THEME_MENU_BG
    MENU_MARK_COLOR = FmrbConst.THEME_TEXT_LIGHT
    BORDER_COLOR = FmrbConst.THEME_BORDER

    def __init__(self):
        self.running = False
        self._suspended = False
        self._close_btn_pressed = False
        # Timers stay None until the app arms one, so an app that never uses
        # them pays nothing per turn (see _run_timers).
        self._timers = None
        self._timer_seq = 0

        info = _fmrb.init()
        self.name = info["name"]
        self.canvas = info["canvas"]
        self.bg_canvas = info["bg_canvas"]
        self.window_width = info["window_width"]
        self.window_height = info["window_height"]
        self.pos_x = info["pos_x"]
        self.pos_y = info["pos_y"]
        self.fullscreen = info["fullscreen"]
        self.rounded_corners = info["rounded_corners"]
        self.headless = info["headless"]
        self.platform = info["platform"]

        if self.canvas is None:
            self.gfx = None
            self.bg_gfx = None
            Log.debug("Headless app: no graphics initialized")
            return

        self.gfx = FmrbGfx(self.canvas, self.window_width, self.window_height)
        self.bg_gfx = (FmrbGfx(self.bg_canvas, self.window_width, self.window_height)
                       if self.bg_canvas is not None else None)

        self._recalc_user_area()

    # ---- window frame ----

    def draw_window_frame(self):
        # The Ruby version bakes this into a GfxBlock so the whole frame is one
        # command; here each primitive is its own command. Same picture, more
        # traffic -- fine at this stage, since the frame is redrawn rarely.
        if self.fullscreen or self.gfx is None:
            return
        g = self.gfx
        w = self.window_width
        h = self.window_height

        # The title is drawn in the built-in font whatever the app selected:
        # the bar is 11 pixels tall, and a 12-pixel Japanese font would hang
        # out of it and into the app's own area.
        saved_font = g.current_font
        if saved_font[0] != FmrbGfx.FONT_DEFAULT:
            g.set_font(FmrbGfx.FONT_DEFAULT)

        # Title bar: rounded rect on top, then square off its bottom edge.
        g.fill_round_rect(0, 0, w, self.TITLE_BAR_H, self.CORNER_R, self.TITLE_BAR_COLOR)
        g.fill_rect(0, self.CORNER_R, w, self.TITLE_BAR_H - self.CORNER_R,
                    self.TITLE_BAR_COLOR)
        # Menu mark (three bars) and title.
        g.fill_rect(3, 3, 9, 1, self.MENU_MARK_COLOR)
        g.fill_rect(3, 5, 9, 1, self.MENU_MARK_COLOR)
        g.fill_rect(3, 7, 9, 1, self.MENU_MARK_COLOR)
        g.draw_text(15, 2, self.name, self.MENU_MARK_COLOR)
        # Close button.
        g.fill_circle(w - self.CLOSE_BTN_CX_OFFSET, self.CLOSE_BTN_CY,
                      self.CLOSE_BTN_R, self.CLOSE_BTN_NORMAL_COLOR)
        # Rounded border.
        g.draw_round_rect(0, 0, w, h, self.CORNER_R, self.BORDER_COLOR)
        self._clear_corners()

        if saved_font[0] != FmrbGfx.FONT_DEFAULT:
            g.set_font(saved_font[0], saved_font[1])

    def _clear_corners(self):
        # Repaint the three pixels outside each corner arc with the canvas
        # colour key, so they composite as transparent. A clear() or a resize
        # leaves opaque pixels there and the corners stop looking round.
        g = self.gfx
        t = self.TRANSPARENT_COLOR
        w = self.window_width
        h = self.window_height
        g.draw_line(0, 0, 1, 0, t)
        g.draw_line(0, 1, 0, 1, t)
        g.draw_line(w - 2, 0, w - 1, 0, t)
        g.draw_line(w - 1, 1, w - 1, 1, t)
        g.draw_line(0, h - 2, 0, h - 1, t)
        g.draw_line(1, h - 1, 1, h - 1, t)
        g.draw_line(w - 1, h - 2, w - 1, h - 1, t)
        g.draw_line(w - 2, h - 1, w - 2, h - 1, t)

    # ---- the system theme (see FmrbConst.THEME_*) ----
    #
    # The five an app actually draws with, under the names the Ruby framework
    # uses, so the two are written the same way.

    def theme_bg(self):
        return FmrbConst.THEME_WINDOW_BG      # page background

    def theme_fg(self):
        return FmrbConst.THEME_TEXT           # ink on theme_bg

    def theme_accent(self):
        return FmrbConst.THEME_HIGHLIGHT      # selection, emphasis

    def theme_border(self):
        return FmrbConst.THEME_BORDER         # rules, boxes, muted text

    def theme_fg_light(self):
        return FmrbConst.THEME_TEXT_LIGHT     # ink on accent / button

    def clear_user_area(self, color=FmrbConst.THEME_WINDOW_BG):
        # Clears inside the frame only, so the title bar and close button
        # survive. Use this rather than gfx.clear. The default is the theme's
        # page colour, so an app that says nothing still matches the desktop.
        if self.gfx is None:
            return
        self.gfx.fill_rect(self.user_area_x0, self.user_area_y0,
                           self.user_area_width, self.user_area_height, color)

    # ---- modifier helpers, for use inside on_event ----
    #
    # The modifier byte is the project's own layout (fmrb_keymap.h), not the
    # USB HID standard one:
    #   bit0=LSHIFT bit1=RSHIFT bit2=LCTRL bit3=RCTRL bit4=LALT bit5=RALT
    # Match letter keys on ev["scancode"] (HID usage id); ev["keycode"] differs
    # between the simulation and the device.

    def ev_ctrl(self, ev):
        return (ev.get("modifier", 0) & 0x0C) != 0

    def ev_shift(self, ev):
        return (ev.get("modifier", 0) & 0x03) != 0

    def ev_alt(self, ev):
        return (ev.get("modifier", 0) & 0x30) != 0

    # ---- lifecycle, override in a subclass ----

    def on_create(self):
        pass

    def on_update(self):
        # Return how long to wait before the next call, in milliseconds.
        return 330

    def on_destroy(self):
        pass

    def on_suspend(self):
        # Another app went fullscreen. This one keeps running but must not draw.
        Log.debug("on_suspend")

    def on_resume(self):
        Log.debug("on_resume")

    def on_resize(self, width, height):
        # The window changed size, most often because fullscreen was toggled.
        # The frame and the contents both have to be drawn again; the user area
        # has already been recalculated when this is called.
        pass

    def on_quit_request(self):
        # Ctrl+Q. Override to ask before closing; the default is to close.
        Log.info("App " + self.name + " quit request")
        self.stop()

    def on_event(self, ev):
        # Close button: press gives feedback, release on the button stops.
        if ev.get("button") != 1:
            return
        kind = ev.get("type")
        if kind != "mouse_down" and kind != "mouse_up":
            return

        cx = self.window_width - self.CLOSE_BTN_CX_OFFSET
        cy = self.CLOSE_BTN_CY
        hit = (abs(ev.get("x", 0) - cx) <= self.CLOSE_BTN_HIT_R and
               abs(ev.get("y", 0) - cy) <= self.CLOSE_BTN_HIT_R)

        if kind == "mouse_down":
            if hit and not self.fullscreen and self.gfx:
                self._close_btn_pressed = True
                self.gfx.fill_circle(cx, cy, self.CLOSE_BTN_R,
                                     self.CLOSE_BTN_PRESSED_COLOR)
                self.gfx.present()
        else:
            if self._close_btn_pressed:
                self._close_btn_pressed = False
                if hit:
                    self.stop()
                elif self.gfx:
                    # Released off the button: put the circle back.
                    self.gfx.fill_circle(cx, cy, self.CLOSE_BTN_R,
                                         self.CLOSE_BTN_NORMAL_COLOR)
                    self.gfx.present()
            elif hit:
                # The press was missed but the release landed on the button.
                self.stop()

    # ---- internals ----

    def _handle_system_control(self, msg):
        cmd = msg.get("cmd")
        if cmd == "suspend":
            self._suspended = True
            self.on_suspend()
            Log.info("App " + self.name + " suspended")
        elif cmd == "resume":
            self._suspended = False
            self.on_resume()
            Log.info("App " + self.name + " resumed")
        elif cmd == "quit_request":
            self.on_quit_request()
        elif cmd == "stop":
            Log.info("App " + self.name + " received stop command")
            self.stop()
        elif cmd == "clear_and_stop":
            Log.info("App " + self.name + " clearing canvas and stopping")
            if self.gfx:
                self.gfx.clear(0x00)
                self.gfx.present()
            self.stop()

    # Called from C when the kernel resizes this app's window. Fullscreen is
    # inferred from the new size rather than sent: the kernel gives an app the
    # whole screen and nothing else that big.
    def _handle_resize(self, width, height, fullscreen):
        self.window_width = width
        self.window_height = height
        # A fullscreen switch says which mode it is; a plain resize (corner
        # drag) says nothing and is windowed either way.
        self.fullscreen = bool(fullscreen)
        self._recalc_user_area()
        if self.gfx:
            self.gfx.canvas_width = width
            self.gfx.canvas_height = height
        if self.bg_gfx:
            self.bg_gfx.canvas_width = width
            self.bg_gfx.canvas_height = height
        Log.info("App " + self.name + " resized to " + str(width) + "x" + str(height))
        self.on_resize(width, height)

    def _recalc_user_area(self):
        if self.fullscreen:
            self.user_area_x0 = 0
            self.user_area_y0 = 0
            self.user_area_x1 = self.window_width
            self.user_area_y1 = self.window_height
            self.user_area_width = self.window_width
            self.user_area_height = self.window_height
        else:
            self.user_area_x0 = 1
            self.user_area_y0 = self.TITLE_BAR_H
            self.user_area_x1 = self.window_width - 1
            self.user_area_y1 = self.window_height - 1
            self.user_area_width = self.window_width - 2
            self.user_area_height = self.window_height - self.TITLE_BAR_H - 1

    # ---- timers ----
    #
    # One-shot, same as the Ruby version: a callback that wants to repeat arms
    # the next one itself. They are checked once per turn of the app loop and
    # again inside each wait (C calls _run_timers from _fmrb.spin), so a timer
    # still fires while the app is parked waiting for a message.

    def set_timer(self, interval_ms, callback):
        self._timer_seq += 1
        timer = (self._timer_seq, ticks_ms() + interval_ms, callback)
        if self._timers is None:
            self._timers = [timer]
        else:
            self._timers.append(timer)
        return self._timer_seq

    def clear_time(self, timer_id):
        timers = self._timers
        if timers is None:
            return
        for i in range(len(timers)):
            if timers[i][0] == timer_id:
                del timers[i]
                return

    # Runs on every turn and inside every wait, so the case of "nothing armed"
    # and "nothing due" must both be cheap: no allocation until a timer
    # actually fires.
    def _run_timers(self):
        timers = self._timers
        if not timers:
            return

        now = ticks_ms()
        due = None
        for t in timers:
            if t[1] <= now:
                if due is None:
                    due = [t]
                else:
                    due.append(t)
        if due is None:
            return

        # Swap the surviving list in before running anything: a callback that
        # arms a new timer has to land in the list that stays.
        self._timers = [t for t in timers if t[1] > now]
        for t in due:
            t[2]()

    # ---- asking the kernel for things ----

    def subscribe(self, topic):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "subscribe", "topic": topic})

    def unsubscribe(self, topic):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "unsubscribe", "topic": topic})

    # Subscribers receive it through on_control as
    # {"cmd": "topic_data", "topic": ..., "data": ...}.
    def publish(self, topic, data=None):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "publish", "topic": topic, "data": data})

    # An app cannot spawn another app, so this is a request: the kernel stops
    # the instance a previous request started (prev_pid, may be None), runs the
    # file and gives it the keyboard. The new pid comes back through on_control
    # as {"cmd": "run_result", "path": ..., "pid": ...}, with pid None when it
    # failed. The kernel limits paths to /app and /home.
    def request_run(self, path, prev_pid=None):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "run", "path": path, "prev_pid": prev_pid})

    # The VM keeps running, so app state survives; the answer arrives as
    # on_resize with the user area already updated.
    def request_fullscreen(self, on):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "enter_fullscreen" if on else "exit_fullscreen"})

    def toggle_fullscreen(self):
        return self.request_fullscreen(not self.fullscreen)

    def request_file_select(self, mode="open"):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "file_select", "mode": mode})

    def request_reload(self):
        return self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                                 {"cmd": "reload_confirm"})

    def send_message(self, dest_pid, msg_type, data):
        return _fmrb.send_message(dest_pid, msg_type, data)

    def set_window_position(self, x, y):
        _fmrb.set_window_pos(x, y)
        self.pos_x = x
        self.pos_y = y
        if self.gfx:
            self.gfx.present()
        return self

    def main_loop(self):
        Log.debug("main_loop started")
        self._suspended = False
        while self.running:
            if self._suspended:
                # Sleep longer while suspended, but keep taking messages: the
                # resume is one of them.
                _fmrb.spin(self, 500)
                continue
            self._run_timers()
            timeout_ms = self.on_update()
            _fmrb.spin(self, timeout_ms)

    def destroy(self):
        # Tell the kernel before releasing anything, so the window disappears
        # from its list while this app can still be talked to.
        try:
            self.send_message(FmrbConst.PROC_ID_KERNEL, FmrbConst.MSG_TYPE_APP_CONTROL,
                              {"cmd": "exit"})
        except Exception as e:
            Log.error("Failed to send exit notification: " + str(e))

        self.on_destroy()
        self.gfx = None
        self.bg_gfx = None
        _fmrb.cleanup()

    def start(self):
        self.running = True
        self.on_create()
        self.main_loop()
        self.destroy()

    def stop(self):
        self.running = False

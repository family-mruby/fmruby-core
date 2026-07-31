# FmrbApp - Family mruby OS Python app framework.
#
# Transcribed from the Ruby version (picoruby-fmrb-app mrblib/fmrb-app.rb) so a
# Python app is written the same way a Ruby one is: subclass, override the
# lifecycle methods, call start(). Attribute names match the Ruby instance
# variables (self.gfx, self.window_width, self.user_area_x0, ...).
#
# First stage only. Deliberately absent rather than stubbed, so calling one
# raises AttributeError instead of quietly doing nothing: suspend/resume
# callbacks beyond the flag, reload, timers, pub/sub, file select, request_run,
# resize handling, extra canvases and scrollbars.

# fmrb_gfx.py is concatenated ahead of this file (see the order in the
# micropython:prelude rake task) and both run in the app's global namespace, so
# FmrbGfx is already defined here. There is no filesystem importer to import it
# from, which is also why the two files are not modules.

import _fmrb


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


class FmrbConst:
    PROC_ID_KERNEL = 0
    MSG_TYPE_APP_CONTROL = 0
    MSG_TYPE_HID_EVENT = 3


class FmrbApp:
    TITLE_BAR_H = 11
    CORNER_R = 4
    TRANSPARENT_COLOR = 0x01

    # Close-button geometry, used by both the frame drawing and the hit test.
    CLOSE_BTN_CX_OFFSET = 6
    CLOSE_BTN_CY = 5
    CLOSE_BTN_R = 3
    CLOSE_BTN_HIT_R = 5
    CLOSE_BTN_NORMAL_COLOR = 0xFF
    CLOSE_BTN_PRESSED_COLOR = 0x49

    TITLE_BAR_COLOR = 0xC5
    MENU_MARK_COLOR = 0xFB
    BORDER_COLOR = 0x60

    def __init__(self):
        self.running = False
        self._suspended = False
        self._close_btn_pressed = False

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

        # Title bar: rounded rect on top, then square off its bottom edge.
        g.fill_round_rect(0, 0, w, self.TITLE_BAR_H, self.CORNER_R, self.TITLE_BAR_COLOR)
        g.fill_rect(0, self.CORNER_R, w, self.TITLE_BAR_H - self.CORNER_R,
                    self.TITLE_BAR_COLOR)
        # Menu mark (three bars) and title.
        g.fill_rect(3, 3, 9, 1, self.MENU_MARK_COLOR)
        g.fill_rect(3, 5, 9, 1, self.MENU_MARK_COLOR)
        g.fill_rect(3, 7, 9, 1, self.MENU_MARK_COLOR)
        g.draw_text(15, 2, self.name, FmrbGfx.WHITE)
        # Close button.
        g.fill_circle(w - self.CLOSE_BTN_CX_OFFSET, self.CLOSE_BTN_CY,
                      self.CLOSE_BTN_R, self.CLOSE_BTN_NORMAL_COLOR)
        # Rounded border.
        g.draw_round_rect(0, 0, w, h, self.CORNER_R, self.BORDER_COLOR)
        self._clear_corners()

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

    def clear_user_area(self, color=FmrbGfx.BLACK):
        # Clears inside the frame only, so the title bar and close button
        # survive. Use this rather than gfx.clear.
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
            Log.info("App " + self.name + " suspended")
        elif cmd == "resume":
            self._suspended = False
            Log.info("App " + self.name + " resumed")
        elif cmd == "stop":
            Log.info("App " + self.name + " received stop command")
            self.stop()
        elif cmd == "clear_and_stop":
            Log.info("App " + self.name + " clearing canvas and stopping")
            if self.gfx:
                self.gfx.clear(0x00)
                self.gfx.present()
            self.stop()

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
                _fmrb.spin(self, 500)
                continue
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

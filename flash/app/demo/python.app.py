# Python demo - the drawing API the first stage of the Python framework offers.
# Click the user area to move to the next page; the close button ends the app.
#
# Written the same way a Ruby app is: subclass FmrbApp, override the lifecycle
# methods, instantiate and start.


class PythonDemoApp(FmrbApp):
    PAGES = ["Shapes", "Lines", "Text"]

    def __init__(self):
        super().__init__()
        self.page = 0

    def on_create(self):
        Log.info("Python demo started on " + self.platform)
        self.draw_window_frame()
        self.draw_page()

    def on_update(self):
        # Nothing animates, so wait a long time between turns and let the
        # message pump do the work.
        return 500

    def on_event(self, ev):
        super().on_event(ev)
        if not self.running:
            return
        if ev.get("type") == "mouse_up" and ev.get("button") == 1:
            if ev.get("y", 0) < self.user_area_y0:
                return  # title bar; the close button is handled above
            self.page = (self.page + 1) % len(self.PAGES)
            self.draw_page()

    def draw_page(self):
        self.clear_user_area(FmrbGfx.BLACK)
        x0 = self.user_area_x0
        y0 = self.user_area_y0
        title = self.PAGES[self.page] + " (click to next)"
        self.gfx.draw_text(x0 + 4, y0 + 2, title, FmrbGfx.WHITE)

        if self.page == 0:
            self.draw_shapes(x0, y0)
        elif self.page == 1:
            self.draw_lines(x0, y0)
        else:
            self.draw_text_page(x0, y0)

        self.gfx.present()

    def draw_shapes(self, x0, y0):
        g = self.gfx
        g.draw_round_rect(x0 + 8, y0 + 16, 60, 30, 6, FmrbGfx.CYAN)
        g.fill_round_rect(x0 + 78, y0 + 16, 60, 30, 6, FmrbGfx.GREEN)
        g.draw_text(x0 + 14, y0 + 48, "round_rect", FmrbGfx.GRAY)

        g.draw_ellipse(x0 + 38, y0 + 76, 28, 18, FmrbGfx.MAGENTA)
        g.fill_ellipse(x0 + 108, y0 + 76, 28, 18, FmrbGfx.RED)
        g.draw_text(x0 + 24, y0 + 100, "ellipse", FmrbGfx.GRAY)

        g.draw_triangle(x0 + 14, y0 + 148, x0 + 62, y0 + 148, x0 + 38, y0 + 116,
                        FmrbGfx.YELLOW)
        g.fill_triangle(x0 + 84, y0 + 148, x0 + 132, y0 + 148, x0 + 108, y0 + 116,
                        FmrbGfx.BLUE)
        g.draw_text(x0 + 22, y0 + 152, "triangle", FmrbGfx.GRAY)

    def draw_lines(self, x0, y0):
        g = self.gfx
        # A fan of lines, with the angle stepped by a list comprehension so the
        # page shows something the Ruby demo cannot express as compactly.
        colors = [FmrbGfx.RED, FmrbGfx.YELLOW, FmrbGfx.GREEN,
                  FmrbGfx.CYAN, FmrbGfx.BLUE, FmrbGfx.MAGENTA]
        cx = x0 + 70
        cy = y0 + 100
        for i, dx in enumerate([-60, -36, -12, 12, 36, 60]):
            g.draw_line(cx, cy, cx + dx, y0 + 24, colors[i % len(colors)])

        g.draw_rect(x0 + 4, y0 + 14, 132, 96, FmrbGfx.GRAY)
        g.draw_text(x0 + 4, y0 + 116, "draw_line x6", FmrbGfx.GRAY)

        # Circles at every third pixel of a range, filled and outlined.
        for i in range(4):
            g.draw_circle(x0 + 20 + i * 30, y0 + 140, 12, FmrbGfx.CYAN)
            g.fill_circle(x0 + 20 + i * 30, y0 + 140, 5, FmrbGfx.WHITE)

    def draw_text_page(self, x0, y0):
        g = self.gfx
        lines = [
            ("MicroPython on", FmrbGfx.WHITE),
            ("Family mruby", FmrbGfx.CYAN),
            ("", FmrbGfx.WHITE),
            ("sum(1..10) = " + str(sum(range(1, 11))), FmrbGfx.GREEN),
            ("2 ** 16 = " + str(2 ** 16), FmrbGfx.YELLOW),
            ("sorted = " + str(sorted([3, 1, 2])), FmrbGfx.MAGENTA),
        ]
        for i, entry in enumerate(lines):
            text, color = entry
            if text:
                g.draw_text(x0 + 8, y0 + 20 + i * 14, text, color)

        g.draw_text(x0 + 8, y0 + 120, "canvas " + str(self.window_width) +
                    "x" + str(self.window_height), FmrbGfx.GRAY)
        g.fill_rect(x0 + 8, y0 + 136, 128, 3, FmrbGfx.RED)
        g.fill_rect(x0 + 8, y0 + 142, 128, 3, FmrbGfx.GREEN)
        g.fill_rect(x0 + 8, y0 + 148, 128, 3, FmrbGfx.BLUE)


PythonDemoApp().start()

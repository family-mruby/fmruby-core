# Drawing half of pysub.app.py, in a second file to show that a Python app can
# be split across files (the importer looks in the app's own directory).
#
# An imported module has its own globals, so the framework's classes are NOT
# visible here: FmrbApp, FmrbGfx and Log are undefined names in this file. What
# the module needs is passed in instead -- here the app itself, which carries
# its gfx and its window measurements. Keeping the framework out of a module is
# also what makes it testable on its own.

WHITE = 0xFF
GRAY = 0x6D
GREEN = 0x1C
YELLOW = 0xFC
BLACK = 0x00

ROW_H = 9


def draw(app, state):
    """Redraw the whole panel. state is the dict pysub.app.py keeps."""
    g = app.gfx
    x = app.user_area_x0 + 3
    y = app.user_area_y0 + 2

    app.clear_user_area(BLACK)

    g.draw_text(x, y, "phase5 check", YELLOW)
    y += ROW_H + 1

    for label, value, color in _rows(state):
        g.draw_text(x, y, label, GRAY)
        g.draw_text(x + 66, y, value, color)
        y += ROW_H

    y += 2
    g.draw_text(x, y, "R:pub S:sub P:send", GRAY)
    g.draw_text(x, y + ROW_H, "E:robot  F:fullscreen", GRAY)

    # The timer flips this twice a second; a stopped blinker means timers
    # stopped running.
    if state["blink"]:
        g.fill_circle(app.user_area_x1 - 8, app.user_area_y0 + 6, 3, GREEN)

    app.draw_window_frame()
    g.present()


def _rows(state):
    got = state["last"]
    return (
        ("received", str(state["count"]), WHITE),
        ("from", got.get("msg", "-") if got else "-", WHITE),
        ("n", str(got.get("n", "-")) if got else "-", WHITE),
        ("list", str(got.get("list", "-")) if got else "-", WHITE),
        ("toml bytes", str(state["toml_bytes"]), WHITE),
        ("uptime s", str(state["uptime"] // 1000), WHITE),
        ("sent", str(state["sent"]), WHITE),
        ("robo view", state["view"], WHITE),
        ("suspended", "yes" if state["suspended"] else "no", WHITE),
    )

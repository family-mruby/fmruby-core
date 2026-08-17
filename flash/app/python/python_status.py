# The system page of python.app.py, in a file of its own.
#
# This is here to be imported: an app can be split across files as long as they
# sit beside it (the search path is the app's own directory and
# /usr/lib/python, nothing else).
#
# What it cannot do is reach the framework. FmrbApp, FmrbGfx, Log, ticks_ms and
# language are set up in the app's namespace, not in a module's, so they are
# undefined names here. Whatever a module needs is passed in -- below, the app
# itself, which carries the values it already collected. Keeping the framework
# out also means this file can be read on its own and says what it does.

import random


def rows(app):
    """Text for the system page: (label, value) pairs, in drawing order."""
    got = app.received
    return (
        ("uptime s", str(app.uptime // 1000)),
        ("language", app.lang),
        ("read_file", str(app.toml_bytes) + " bytes"),
        ("random", str([random.randint(0, 9) for _ in range(4)])),
        ("published", str(app.sent)),
        ("received", str(got.get("n", "-")) if got else "-"),
        ("from", str(got.get("msg", "-")) if got else "-"),
        ("list", str(got.get("list", "-")) if got else "-"),
    )

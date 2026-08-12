# Shared editor constants (colors, layout, fonts, menu ids, scancodes, timings).
#
# Split out of editor.app.rb (doc/editor_refactor). Every editor mixin that
# uses these includes EditorConst, so it reaches them by their bare names --
# a module's constants are found through the ancestor chain the include adds
# (standard Ruby; the Spinel build treats constants as flat globals, so it is a
# no-op there). This is what lets the mixins drop the  prefix that
# an including-class constant would otherwise require.
module EditorConst

  # Colors - Light pink theme
  BG_COLOR      = FmrbGfx.rgb_to_332(255, 230, 240)  # Nearly white pink
  TEXT_COLOR     = FmrbGfx.rgb_to_332(0, 0, 0)        # Black text
  MENU_BG       = FmrbGfx.rgb_to_332(100, 60, 100)    # Dark purple menu bar
  MENU_TEXT     = FmrbGfx.rgb_to_332(255, 255, 255)   # White menu text
  MENU_KEY      = FmrbGfx.rgb_to_332(255, 255, 0)     # Yellow hotkey
  STATUS_BG     = FmrbGfx.rgb_to_332(40, 40, 60)      # Dark gray status line
  STATUS_TEXT   = FmrbGfx.rgb_to_332(255, 255, 255)   # White status text
  STATUS_OK_BG  = FmrbGfx.rgb_to_332(0, 160, 0)       # Green flash for save success
  STATUS_OK_TEXT = FmrbGfx.rgb_to_332(255, 255, 255)
  PROBLEM_BADGE_TEXT = FmrbGfx.rgb_to_332(255, 120, 120)  # Problem count badge
  PROBLEM_BG    = FmrbGfx.rgb_to_332(255, 190, 190)   # Row tint of a problem line
  CURSOR_COLOR  = FmrbGfx.rgb_to_332(0, 0, 200)       # Blue cursor

  # Syntax highlight colors (for light background)
  HL_COLORS = [
    FmrbGfx.rgb_to_332(0, 0, 0),        # 0: default    - black
    FmrbGfx.rgb_to_332(180, 0, 0),      # 1: keyword    - dark red
    FmrbGfx.rgb_to_332(0, 120, 0),      # 2: string     - dark green
    FmrbGfx.rgb_to_332(120, 120, 120),  # 3: comment    - gray
    FmrbGfx.rgb_to_332(160, 100, 0),    # 4: number     - brown
    FmrbGfx.rgb_to_332(140, 0, 140),    # 5: symbol     - dark magenta
    FmrbGfx.rgb_to_332(0, 100, 140),    # 6: constant   - dark cyan
    FmrbGfx.rgb_to_332(180, 0, 60),     # 7: variable   - crimson
    FmrbGfx.rgb_to_332(0, 0, 180),      # 8: method     - dark blue
  ]

  # Printable ASCII indexed by (code - 32). Used instead of Integer#chr, which
  # the Spinel runtime does not provide -- and a table lookup is the same in both
  # builds, so the source stays single-backend.
  ASCII_PRINTABLE = " !\"\#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

  CHAR_W = 6
  # The edit area is a fixed grid of cells in efontJA_12: a half-width glyph is
  # exactly one 6px cell and a full-width one exactly two, so the classic
  # terminal model holds with no fractional positions anywhere. The chrome
  # (menu bar, status line, dialogs) stays on the 6x8 default font.
  CELL_W = 6
  LINE_H = 12
  EDIT_FONT_SIZE = 12
  # draw_text carries FMRB_GFX_MAX_TEXT_LEN (128) bytes per command and a
  # Japanese character is three of them, so a row is emitted in several
  # commands. Kept under the limit rather than at it: the split only ever
  # happens on a character boundary.
  DRAW_TEXT_MAX_BYTES = 120
  CHAR_H = 8
  TAB_SIZE = 2

  # Menu dropdown common style
  DROPDOWN_BG = FmrbGfx.rgb_to_332(255, 255, 255)
  DROPDOWN_TEXT = FmrbGfx.rgb_to_332(0, 0, 0)
  DROPDOWN_SEL_BG = FmrbGfx.rgb_to_332(100, 60, 100)
  DROPDOWN_SEL_TEXT = FmrbGfx.rgb_to_332(255, 255, 255)
  DROPDOWN_ITEM_H = 10

  # Per-menu config: scancode hotkeys (labels come from FmrbI18n).
  # Hotkey scancodes pick a distinguishing letter per item (DOS-Edit style) and
  # are the same in every language -- only the words next to them change.
  MENU_FILE_HOTKEYS  = [0x12, 0x16, 0x04, 0x17, 0x1B]  # O, S, A, T, X

  # App skeletons, as files so a user can add their own next to the shipped
  # ones. File > Template lists this directory and inserts the chosen file at
  # the cursor.
  TEMPLATE_DIR = "/lib/templates"

  MENU_EDIT_HOTKEYS  = [0x17, 0x06, 0x13, 0x04]  # T (cuT), C, P, A

  # Menu bar items. Integer ids rather than symbols so the parallel position and
  # width arrays built while drawing stay concretely typed in both engines.
  MENU_ID_FILE    = 0
  MENU_ID_EDIT    = 1
  MENU_ID_SEARCH  = 2
  MENU_ID_RUN     = 3
  MENU_ID_HILIGHT = 4
  MENU_ID_WRAP    = 5
  MENU_ID_DEBUG   = 6
  MENU_ID_FULL    = 7
  MENU_BAR_IDS  = [MENU_ID_FILE, MENU_ID_EDIT, MENU_ID_SEARCH, MENU_ID_RUN,
                   MENU_ID_HILIGHT, MENU_ID_WRAP, MENU_ID_DEBUG, MENU_ID_FULL]
  # Accelerator letter shown in parentheses after each label. Empty means the
  # item has no letter (Full is a direct toggle on F11).
  MENU_BAR_KEYS = ["F", "E", "S", "R", "H", "W", "D", ""]
  MENU_BAR_GAP  = 6   # px between menu bar items

  # Selection / clipboard colors
  SEL_BG = FmrbGfx.rgb_to_332(180, 200, 255)  # Light blue selection

  # Quit-confirm dialog frame. Shared: editor/render.rb draws the quit dialog
  # and editor/search.rb reuses the same frame for its own.
  QUIT_DLG_BG     = FmrbGfx.rgb_to_332(255, 255, 255)
  QUIT_DLG_BORDER = FmrbGfx.rgb_to_332(0, 0, 0)
  QUIT_DLG_TEXT   = FmrbGfx.rgb_to_332(0, 0, 0)
  QUIT_DLG_KEY    = FmrbGfx.rgb_to_332(180, 0, 0)

  # Key repeat timing (in frames, ~33ms each)
  KEY_REPEAT_DELAY = 12  # ~400ms before repeat starts
  KEY_REPEAT_RATE = 3    # ~100ms between repeats

  STATUS_MSG_FRAMES = 150 # ~5s before a status message gives the line back

  # ---- Input latency instrumentation (doc/editor_serious_mode/plan.md) ----
  # Time from a key event to the present that shows its effect. Always on: one
  # Machine.uptime_us per key and per redraw, integer accumulators, and a single
  # log line every LAT_REPORT_N samples -- so the measurement itself neither
  # allocates in the hot path nor changes what it measures.
  LAT_REPORT_N = 100
  LAT_SLOW_US = 25_000   # "over 25ms" bucket, the target says zero of these
  LAT_BUCKET_US = 5_000  # histogram resolution
  LAT_BUCKETS = 10       # last bucket is the >= 50ms overflow

  # Search dialog

  # Layout bits the debugger's gutter needs; kept here because the edit area
  # geometry uses them whether or not a debug session exists.
  GUTTER_W  = 8                                 # gutter width in px (debug mode)
  GUTTER_BG = FmrbGfx.rgb_to_332(210, 195, 205) # gutter column background
  # Function-key scancodes (USB HID Usage IDs). F5 = Run and F11 = fullscreen
  # belong to the editor; the rest are the debugger's, used from its mixin.
  SC_F4 = 0x3D; SC_F5 = 0x3E; SC_F6 = 0x3F; SC_F7 = 0x40
  SC_F8 = 0x41; SC_F9 = 0x42; SC_F10 = 0x43; SC_F11 = 0x44
  # Tab, and the keys the completion list answers to (HID Usage IDs).
  SC_TAB = 0x2B; SC_ENTER = 0x28; SC_KP_ENTER = 0x58; SC_ESC = 0x29
  SC_UP = 0x52; SC_DOWN = 0x51
  # Ctrl+T shows the type under the cursor, Ctrl+E the type errors. Neither
  # letter was taken: the editor uses Ctrl+S/X/C/V/A/D, and Ctrl+Q, Ctrl+Tab
  # and Ctrl+Space belong to the system.
  SC_T = 0x17; SC_E = 0x08
  # F1 opens the help page for the method under the cursor (or the selected
  # candidate). Nothing else used it: the debugger's keys start at F4.
  SC_F1 = 0x3A

end

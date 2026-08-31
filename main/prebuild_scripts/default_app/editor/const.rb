# Shared editor constants (colors, layout, fonts, menu ids, scancodes, timings).
#
# Split out of editor.app.rb (doc/editor_refactor). Every editor mixin that
# uses these includes EditorConst, so it reaches them by their bare names --
# a module's constants are found through the ancestor chain the include adds
# (standard Ruby; the Spinel build treats constants as flat globals, so it is a
# no-op there). This is what lets the mixins drop the  prefix that
# an including-class constant would otherwise require.
module EditorConst

  # Colors.
  #
  # The chrome (page, ink, menu bar, status line, selection, cursor) follows
  # the system theme, so changing [theme] restyles the editor with everything
  # else. A user who wants the editor to differ says so in /home/colors.toml
  # ([editor] bg / text / menu_bg / menu_text / status_bg / status_text /
  # selection / cursor), which wins here and nowhere else.
  #
  # What does NOT come from the theme is below: the syntax colours, which
  # carry meaning rather than style, and the two problem tints.
  EDITOR_COLORS = FmrbColors.section("editor")
  BG_COLOR      = EDITOR_COLORS["bg"] || FmrbConst::THEME_WINDOW_BG
  TEXT_COLOR    = EDITOR_COLORS["text"] || FmrbConst::THEME_TEXT
  MENU_BG       = EDITOR_COLORS["menu_bg"] || FmrbConst::THEME_MENU_BG
  MENU_TEXT     = EDITOR_COLORS["menu_text"] || FmrbConst::THEME_TEXT_LIGHT
  MENU_KEY      = FmrbGfx.rgb_to_332(255, 255, 0)     # Yellow hotkey
  # The same role on a light panel (the key list): yellow on white is unreadable.
  MENU_KEY_DARK = FmrbGfx.rgb_to_332(120, 60, 0)
  STATUS_BG     = EDITOR_COLORS["status_bg"] || FmrbConst::THEME_MENU_BG
  STATUS_TEXT   = EDITOR_COLORS["status_text"] || FmrbConst::THEME_TEXT_LIGHT
  STATUS_OK_BG  = FmrbGfx.rgb_to_332(0, 160, 0)       # Green flash for save success
  STATUS_OK_TEXT = FmrbGfx.rgb_to_332(255, 255, 255)
  PROBLEM_BADGE_TEXT = FmrbGfx.rgb_to_332(255, 120, 120)  # Problem count badge
  PROBLEM_BG    = FmrbGfx.rgb_to_332(255, 190, 190)   # Row tint of a problem line
  # The cursor has to stand out against the page, so it follows the ink
  # rather than a colour of its own -- a blue cursor on a dark theme was
  # invisible.
  CURSOR_COLOR  = EDITOR_COLORS["cursor"] || FmrbConst::THEME_TEXT

  # Syntax highlight colors. These are meaning, not style, so they do not
  # come from the theme -- but they have to be readable ON it, and the light
  # set disappears against a dark page (dark blue on black). So there are two
  # sets and the page's own brightness picks one. Slot 0 is the ordinary text
  # and always follows the ink, or the whole file would be invisible the
  # moment a dark theme was chosen.
  #
  # Brightness of an RGB332 colour, weighted the way an eye is: green counts
  # most, blue least. 0..104; below 45 is a dark page.
  BG_LUMA = ((BG_COLOR >> 5) & 7) * 5 + ((BG_COLOR >> 2) & 7) * 9 + (BG_COLOR & 3) * 2
  HL_DARK = [
    TEXT_COLOR,                          # 0: default    - the page's ink
    FmrbGfx.rgb_to_332(255, 120, 120),   # 1: keyword
    FmrbGfx.rgb_to_332(120, 255, 120),   # 2: string
    FmrbGfx.rgb_to_332(160, 160, 160),   # 3: comment
    FmrbGfx.rgb_to_332(255, 200, 100),   # 4: number
    FmrbGfx.rgb_to_332(255, 140, 255),   # 5: symbol
    FmrbGfx.rgb_to_332(120, 220, 255),   # 6: constant
    FmrbGfx.rgb_to_332(255, 140, 180),   # 7: variable
    FmrbGfx.rgb_to_332(140, 170, 255),   # 8: method
  ]
  HL_LIGHT = [
    TEXT_COLOR,                          # 0: default    - the page's ink
    FmrbGfx.rgb_to_332(180, 0, 0),       # 1: keyword    - dark red
    FmrbGfx.rgb_to_332(0, 120, 0),       # 2: string     - dark green
    FmrbGfx.rgb_to_332(120, 120, 120),   # 3: comment    - gray
    FmrbGfx.rgb_to_332(160, 100, 0),     # 4: number     - brown
    FmrbGfx.rgb_to_332(140, 0, 140),     # 5: symbol     - dark magenta
    FmrbGfx.rgb_to_332(0, 100, 140),     # 6: constant   - dark cyan
    FmrbGfx.rgb_to_332(180, 0, 60),      # 7: variable   - crimson
    FmrbGfx.rgb_to_332(0, 0, 180),       # 8: method     - dark blue
  ]
  HL_COLORS = BG_LUMA < 45 ? HL_DARK : HL_LIGHT

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
  DROPDOWN_BG = BG_COLOR
  DROPDOWN_TEXT = TEXT_COLOR
  DROPDOWN_SEL_BG = FmrbConst::THEME_HIGHLIGHT
  DROPDOWN_SEL_TEXT = FmrbConst::THEME_TEXT_LIGHT
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
  MENU_ID_VIEW    = 4
  MENU_ID_DEBUG   = 5
  MENU_ID_KEYS    = 6
  # Highlight, wrap and fullscreen used to sit on the bar as three toggles of
  # their own, which is what made the row too long to fit a 240px window. They
  # are inside View now, where their state reads as [x] rather than a trailing
  # "*", and their direct keys (Alt-H, Alt-W, F11) are unchanged.
  MENU_BAR_IDS  = [MENU_ID_FILE, MENU_ID_EDIT, MENU_ID_SEARCH, MENU_ID_RUN,
                   MENU_ID_VIEW, MENU_ID_DEBUG, MENU_ID_KEYS]
  # Accelerator letter shown in parentheses after each label. Keys is K: F1 is
  # already the API help for the symbol under the cursor, and H and W still
  # toggle highlight and wrap.
  MENU_BAR_KEYS = ["F", "E", "S", "R", "V", "D", "K"]

  # View dropdown: the three display toggles, in the order they were on the bar.
  MENU_VIEW_HOTKEYS = [0x0B, 0x1A, 0x09]  # H, W, F
  MENU_BAR_GAP  = 6   # px between menu bar items

  # Selection / clipboard colors
  SEL_BG = EDITOR_COLORS["selection"] || FmrbConst::THEME_HIGHLIGHT

  # Quit-confirm dialog frame. Shared: editor/render.rb draws the quit dialog
  # and editor/search.rb reuses the same frame for its own.
  QUIT_DLG_BG     = BG_COLOR
  QUIT_DLG_BORDER = FmrbConst::THEME_BORDER
  QUIT_DLG_TEXT   = TEXT_COLOR
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
  GUTTER_BG = FmrbConst::THEME_BUTTON            # gutter column background
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

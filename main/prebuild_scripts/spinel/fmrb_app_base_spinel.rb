# Spinel base layer for application VMs (system_desktop and, later, other
# Spinel apps).
#
# The mruby app framework gets FmrbApp / FmrbGfx / GfxBlock / Log / Machine from
# C extensions and mrblib (picoruby-fmrb-app). A Spinel-compiled app has no such
# layer, so this file re-implements the pieces the app framework needs, on top of
# the FmrbSpxApp / FmrbSpxGfx FFI shims (fmrb_app_ffi.rb / main/app/fmrb_spx_*.c).
# The dual-build seam is confined here; system_desktop.app.rb and its mixins sit
# on top unchanged.
#
# Spliced before this file in the combined build (T4-3):
#   fmrb_ffi.rb (FmrbSpx: reused for Log / board_millis),
#   fmrb_app_ffi.rb (FmrbSpxApp / FmrbSpxGfx),
#   fmrb_const_generated.rb (FmrbConst), msgpack_pure.rb (MessagePack)
#
# All the language features used here (keyword args, block.call with kwargs,
# Enumerable#partition, Math.sqrt, splat def, String#bytes, setbyte, ::Const)
# were verified against the Spinel compiler before authoring.

module Log
  # Return nil (not the :void FFI result): a method whose tail is a Log call is
  # otherwise inferred to return void, and using its value (e.g. a rescue arm
  # feeding an expression) trips Spinel's "void value not ignored".
  def self.debug(msg); FmrbSpx.fmrb_spx_log_write(0, msg, msg.bytesize); nil; end
  def self.info(msg);  FmrbSpx.fmrb_spx_log_write(1, msg, msg.bytesize); nil; end
  def self.warn(msg);  FmrbSpx.fmrb_spx_log_write(2, msg, msg.bytesize); nil; end
  def self.error(msg); FmrbSpx.fmrb_spx_log_write(3, msg, msg.bytesize); nil; end
end

module Machine
  def self.board_millis
    FmrbSpx.fmrb_spx_board_millis
  end

  # Microseconds from the same monotonic clock. Named to match the picoruby
  # machine gem so app code (the editor's edit_lat instrumentation) reads the
  # same on both engines.
  def self.uptime_us
    FmrbSpx.fmrb_spx_board_micros
  end

  # Coarse busy wait against board_millis (used by the desktop boot animation
  # for frame pacing). Each Spinel app is its own preemptive FreeRTOS task, so
  # this only spins the app's own task; a vTaskDelay FFI would be lighter and is
  # a future improvement (see phase4 report).
  def self.delay_ms(ms)
    target = board_millis + ms
    while board_millis < target
      # spin
    end
    nil
  end
end

# Minimal FmrbKernel for the Spinel app VM. In the mruby build FmrbKernel is the
# kernel's global C-extension class (available to every VM); a Spinel app program
# has no such class, so provide the subset apps call. system_desktop only calls
# boot_complete! (status LED: fast-blink -> heartbeat).
module FmrbKernel
  def self.boot_complete!
    FmrbSpxApp.fmrb_spx_app_boot_complete
    nil
  end
end

# Little-endian readers/writers and NUL-padded name fields over byte Strings.
# NOTE: never define a helper named `name` here -- Spinel resolves e.g.
# `SpxBytes.name` to the built-in Module#name, silently corrupting parsing
# (Phase 2 GUI bug). Use distinct verbs (read_name).
module SpxBytes
  def self.u16(s, off)
    s.getbyte(off) | (s.getbyte(off + 1) << 8)
  end

  def self.u32(s, off)
    s.getbyte(off) | (s.getbyte(off + 1) << 8) |
      (s.getbyte(off + 2) << 16) | (s.getbyte(off + 3) << 24)
  end

  def self.i16(s, off)
    v = u16(s, off)
    v >= 0x8000 ? v - 0x10000 : v
  end

  def self.i32(s, off)
    v = u32(s, off)
    v >= 0x80000000 ? v - 0x100000000 : v
  end

  # Copy `len` bytes from offset into a fresh String (Spinel has no sp_str_chr,
  # so build with setbyte rather than Integer#chr).
  def self.slice(s, off, len)
    out = "\x00" * len
    i = 0
    while i < len
      out.setbyte(i, s.getbyte(off + i))
      i += 1
    end
    out
  end

  # NUL-padded fixed-width field -> String (stops at first NUL).
  def self.read_name(s, off, width)
    len = 0
    while len < width && s.getbyte(off + len) != 0
      len += 1
    end
    slice(s, off, len)
  end

  def self.set_u16(s, off, v)
    s.setbyte(off, v & 0xFF)
    s.setbyte(off + 1, (v >> 8) & 0xFF)
  end

  def self.set_i16(s, off, v)
    set_u16(s, off, v & 0xFFFF)
  end
end

# Graphics API: FFI reimplementation of gfx.c (C methods) + fmrb-gfx.rb (Ruby
# wrapper). The canvas id is a plain instance variable (no boxed mrb_gfx_data).
# GC.start is a Spinel built-in (a full collection). GC.step is the mruby
# incremental collector's single step; Spinel's collector has no such phase,
# so the desktop's time-boxed stepping loop is a no-op here, the same call the
# mruby build makes and nothing more.
class GC
  def self.step; nil; end
end

# Asked at call time rather than frozen into the generated constants: on the
# Modern board the Bluetooth address belongs to a separate radio chip and is
# known only once the BLE host has synced. "-" until then, and on Linux.
module FmrbConst
  def self.bt_mac
    FmrbSpxApp.fmrb_spx_app_bt_mac
  end
end

# Per-app colour overrides: /home/colors.toml.
#
# [theme] in system_conf.toml sets the look of the whole machine, and every
# app takes its defaults from there. This is the one place a user can
# disagree with it for a single app -- without a rebuild, and without the
# reboot the system theme needs. A missing file, a missing section and a
# missing key all mean "use the theme", so the file only ever holds what
# somebody deliberately changed, and deleting a line is how you go back.
#
#   [editor]
#   bg = "midnightblue"   # a colour name, or
#   text = 0xFC           # RGB332, as in system_conf.toml
#   [shell]
#   bg = black
#
# It lives in /home because it is the user's: on the browser build that is
# also the only directory that survives a reload, and it travels in the
# exported tar with everything else they made.
#
# Read once, where an app resolves its colour constants. The parser is the
# same small toml subset SvcConf.parse handles, for the same reason: there is
# no Ruby binding for the C toml reader and this is twenty lines. Base-16
# String#to_i and String#downcase are avoided on purpose -- the digits are
# looked up by hand so this file compiles under Spinel as well as mruby.
module FmrbColors
  PATH = "/home/colors.toml"
  HEX = "0123456789abcdefABCDEF"

  # ---- colour names -------------------------------------------------------
  #
  # The names are the web's, which is the one palette vocabulary most people
  # already have, mapped onto the 256 colours this machine can show. Doing
  # that collapses some of them (a 3-3-2 bit grid cannot tell ivory from
  # snow), so the list is in two parts: PALETTE is one name per distinct
  # colour -- what a picker offers -- and ALIAS holds the other spellings
  # that land on a colour already in it, so a name somebody knows is
  # understood even when it is not the one offered back.
  #
  # Two string literals rather than a hash of 101 entries: about a kilobyte,
  # no objects, and the only lookups happen when a colours file is read or a
  # picker is open. Nothing here is on a drawing path.
  PALETTE_NAMES =
    " black white red lime blue yellow cyan magenta silver gray maroon " \
    "olive green purple teal navy orange gold pink hotpink deeppink " \
    "crimson firebrick tomato salmon orangered darkorange khaki tan peru " \
    "chocolate saddlebrown sienna darkkhaki yellowgreen olivedrab " \
    "greenyellow chartreuse springgreen limegreen forestgreen darkgreen " \
    "seagreen mediumseagreen lightgreen darkseagreen aquamarine turquoise " \
    "darkturquoise lightseagreen lightcyan paleturquoise powderblue " \
    "skyblue deepskyblue dodgerblue cornflowerblue steelblue royalblue " \
    "midnightblue slateblue mediumpurple blueviolet darkorchid " \
    "mediumorchid orchid violet plum thistle lavender indigo " \
    "mediumvioletred palevioletred mistyrose lightgray dimgray " \
    "lightslategray darkslategray "
  PALETTE_VALUES = "\x00\xFF\xE0\x1C\x03\xFC\x1F\xE3\xB6\x92\x80\x90\x10\x82\x12\x02\xF4\xF8\xF6\xEE\xE6\xC5\xA4\xED\xF1\xE8\xF0\xFA\xD6\xD1\xCC\x88\x89\xB5\x99\x70\xBD\x7C\x1D\x39\x30\x0C\x31\x55\x9E\x96\x7E\x5A\x1A\x36\xDF\xBF\xBB\x9B\x17\x33\x73\x52\x4F\x25\x6A\x8F\x87\x86\xAA\xCF\xF3\xD3\xD7\xDB\x42\xA6\xCE\xFB\xDA\x6D\x72\x29"

  ALIAS_NAMES =
    " darkred coral lightyellow ivory beige wheat brown lawngreen " \
    "palegreen darkcyan mediumblue darkblue darkviolet darkmagenta " \
    "lightpink darkgray slategray gainsboro whitesmoke snow azure " \
    "honeydew linen "
  ALIAS_VALUES = "\x80\xED\xFF\xFF\xFF\xFA\xA4\x7C\x9E\x12\x02\x02\x82\x82\xF6\xB6\x72\xDB\xFF\xFF\xFF\xFF\xFF"
  # 0..255 for a name, nil when it is not one of ours. The names strings
  # carry a leading and a trailing space so " name " can be searched for
  # without building a padded copy.
  def self.by_name(name)
    v = lookup(PALETTE_NAMES, PALETTE_VALUES, name)
    return v unless v.nil?
    lookup(ALIAS_NAMES, ALIAS_VALUES, name)
  end

  def self.lookup(names, values, name)
    at = names.index(" " + name + " ")
    return nil if at.nil?
    i = 0
    n = 0
    while i < at
      n += 1 if names.getbyte(i) == 32
      i += 1
    end
    values.getbyte(n)
  end

  # The name a picker should show for a colour, or nil for one with no name.
  def self.name_of(value)
    i = 0
    while i < PALETTE_VALUES.bytesize
      return name_at(i) if PALETTE_VALUES.getbyte(i) == value
      i += 1
    end
    nil
  end

  # The nth name in PALETTE_NAMES.
  def self.name_at(index)
    i = 1                     # skip the leading space
    n = 0
    start = 1
    while i < PALETTE_NAMES.bytesize
      if PALETTE_NAMES.getbyte(i) == 32
        return PALETTE_NAMES[start, i - start] if n == index
        n += 1
        start = i + 1
      end
      i += 1
    end
    nil
  end

  # The value strings are bytes, not text: String#length would count them as
  # UTF-8 and merge some (0xE0 starts a three-byte sequence), so every walk
  # over them uses bytesize and getbyte.
  def self.palette_size
    PALETTE_VALUES.bytesize
  end

  def self.palette_value(index)
    PALETTE_VALUES.getbyte(index)
  end


  # A step darker than a theme colour, for a bar that must not merge with what
  # is next to it: the editor's menu row sits directly under the window title
  # bar, and with both on THEME_MENU_BG there was no edge between them.
  #
  # Down two of the seven red/green steps and one of the three blue ones, then
  # three results are refused and sent the other way instead:
  #   0x01   the desktop's transparent key -- a window painted with it goes
  #          see-through (that one becomes plain black)
  #   the colour itself, which is what a colour already at the bottom gives
  #   `avoid`, for the caller's other neighbour: on the neon theme the menu
  #          colour darkens to exactly the page colour, so the bar would have
  #          vanished into the page instead of into the title bar
  # The point is to differ from both, not to be dark.
  def self.shade(color, avoid = nil)
    r = (color >> 5) & 7
    g = (color >> 2) & 7
    b = color & 3
    dr = r > 1 ? r - 2 : 0
    dg = g > 1 ? g - 2 : 0
    db = b > 0 ? b - 1 : 0
    out = (dr << 5) | (dg << 2) | db
    out = 0x00 if out == 0x01
    if out == color || out == avoid
      ur = r < 6 ? r + 2 : 7
      ug = g < 6 ? g + 2 : 7
      ub = b < 3 ? b + 1 : 3
      up = (ur << 5) | (ug << 2) | ub
      out = up unless up == avoid || up == color
    end
    out
  end

  # ---- the file -----------------------------------------------------------

  # { "bg" => 0x24, ... } for one section; empty when there is nothing to say.
  def self.section(name)
    parse(read, name)
  end

  def self.read
    text = nil
    begin
      f = File.open(PATH, "r")
      text = f.read
      f.close
    rescue => e
      return nil
    end
    text
  end

  def self.parse(text, name)
    out = {}
    return out if text.nil?
    want = "[" + name + "]"
    inside = false
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.length == 0
      next if line.start_with?("#")
      if line.start_with?("[")
        inside = (line == want)
        next
      end
      next unless inside
      eq = line.index("=")
      next if eq.nil?
      key = line[0, eq].strip
      val = line[eq + 1, line.length - eq - 1].strip
      cut = val.index("#")
      val = val[0, cut].strip unless cut.nil?
      next if key.length == 0 || val.length == 0
      col = to_color(val)
      out[key] = col unless col.nil?
    end
    out
  end

  # "0x24", "36", "black" or "\"black\"" -> 36. nil for anything else, so a
  # typo leaves the theme colour in place instead of painting the app black.
  def self.to_color(val)
    if val.start_with?("\"") && val.length > 1 && val.end_with?("\"")
      val = val[1, val.length - 2]
    end
    return nil if val.length == 0
    if val.start_with?("0x") || val.start_with?("0X")
      n = 0
      i = 2
      digits = 0
      while i < val.length
        d = HEX.index(val[i, 1])
        return nil if d.nil?
        d -= 6 if d > 15
        n = n * 16 + d
        digits += 1
        i += 1
      end
      return nil if digits == 0
      return n & 0xFF
    end
    first = val.getbyte(0)
    if first >= 48 && first <= 57      # a digit: a plain number
      i = 0
      while i < val.length
        b = val.getbyte(i)
        return nil if b < 48 || b > 57
        i += 1
      end
      return val.to_i & 0xFF
    end
    by_name(val)
  end

  # ---- writing it back ----------------------------------------------------
  #
  # Line-level edits of whatever is already in the file, so comments, the
  # other app's section and keys nobody here knows about all survive -- the
  # same rule the system Config dialog follows for system_conf.toml. A file
  # somebody wrote by hand must still look like theirs afterwards.

  # value nil removes the key. Returns true when the file was written.
  def self.set(section, key, value)
    want = "[" + section + "]"
    text = read
    lines = text.nil? ? [] : text.split("\n")
    seen = false
    i = 0
    while i < lines.size
      seen = true if lines[i].strip == want
      i += 1
    end
    out = []
    in_sec = false
    placed = false
    i = 0
    while i < lines.size
      line = lines[i]
      t = line.strip
      i += 1
      if t.start_with?("[")
        if in_sec && !placed && !value.nil?
          out << (key + " = " + to_text(value))
          placed = true
        end
        in_sec = (t == want)
        out << line
        next
      end
      if in_sec && !t.start_with?("#")
        eq = t.index("=")
        if !eq.nil? && t[0, eq].strip == key
          if !value.nil? && !placed
            out << (key + " = " + to_text(value))
            placed = true
          end
          next                  # the old line goes, duplicates with it
        end
      end
      out << line
    end
    if !value.nil? && !placed
      out << "" if out.size > 0 && out[out.size - 1].strip.length > 0 && !seen
      out << want unless seen
      out << (key + " = " + to_text(value))
    end
    write(out)
  end

  # Everything this app had to say goes away, so it follows the theme again.
  def self.clear(section)
    want = "[" + section + "]"
    text = read
    return true if text.nil?
    out = []
    in_sec = false
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i]
      t = line.strip
      i += 1
      if t.start_with?("[")
        in_sec = (t == want)
        next if in_sec
      end
      next if in_sec
      out << line
    end
    write(out)
  end

  def self.write(lines)
    body = ""
    i = 0
    while i < lines.size
      body = body + lines[i] + "\n"
      i += 1
    end
    begin
      f = File.open(PATH, "w")
      f.write(body)
      f.close
    rescue => e
      return false
    end
    true
  end

  # The text to write back for a colour: its name when it has one, so a file
  # a person edits stays a file a person can read.
  def self.to_text(value)
    n = name_of(value)
    return n unless n.nil?
    "0x" + HEX[(value >> 4) & 15, 1] + HEX[value & 15, 1]
  end
end

class FmrbGfx
  # Color constants (RGB332). Both the gfx.c names (WHITE...) and the
  # fmrb-gfx.rb names (COLOR_WHITE...) are provided so callers of either work.
  BLACK   = 0x00
  WHITE   = 0xFF
  RED     = 0xE0
  GREEN   = 0x1C
  BLUE    = 0x03
  YELLOW  = 0xFC
  CYAN    = 0x1F
  MAGENTA = 0xE3
  GRAY    = 0x6D
  COLOR_BLACK   = 0x00
  COLOR_WHITE   = 0xFF
  COLOR_RED     = 0xE0
  COLOR_GREEN   = 0x1C
  COLOR_BLUE    = 0x03
  COLOR_YELLOW  = 0xFC
  COLOR_CYAN    = 0x1F
  COLOR_MAGENTA = 0xE3
  COLOR_GRAY    = 0x6D

  BLEND_ADD = 0
  BLEND_XOR = 1

  # char_w is the full-width (CJK) cell, half_w the ASCII cell. Both :ja fonts
  # are dual width -- misaki draws ASCII at 4px and kanji at 8, efont at 6 and
  # 12 -- and treating ASCII as full width made every measurement of a mixed
  # string too wide, which is what places a menu label or a truncation point.
  #
  # kana_w is half-width katakana (U+FF61-U+FF9F) and is NOT simply half_w:
  # misaki has those glyphs and draws them in a half cell, efontJA_12 does not
  # and falls back to a full-width box. Measured on the simulator with
  # flash/app/test/ja_width.app.rb, "ｱｲｳｴｵ|": misaki 26px (5x4 + 6),
  # efont 66px (5x12 + 6) -- the same as five kanji.
  FONT_METRICS = {
    [:default] => { char_w: 6,  half_w: 6, kana_w: 4,  line_h: 8  },
    [:ja, 8]   => { char_w: 8,  half_w: 4, kana_w: 4,  line_h: 8  },
    [:ja, 12]  => { char_w: 12, half_w: 6, kana_w: 12, line_h: 12 },
  }

  attr_reader :current_font, :current_text_size, :canvas_width, :canvas_height, :canvas_id

  def initialize(canvas_id, width: 0, height: 0)
    @canvas_id = canvas_id
    @canvas_width = width
    @canvas_height = height
    @current_font = [:default]
    @current_text_size = 1
  end

  # ---- primitives (forward to the gfx shim with @canvas_id) ----
  def clear(color); FmrbSpxGfx.fmrb_spx_gfx_clear(@canvas_id, color); self; end
  def set_pixel(x, y, color); FmrbSpxGfx.fmrb_spx_gfx_set_pixel(@canvas_id, x, y, color); self; end
  def draw_line(x1, y1, x2, y2, color); FmrbSpxGfx.fmrb_spx_gfx_draw_line(@canvas_id, x1, y1, x2, y2, color); self; end
  def draw_rect(x, y, w, h, color); FmrbSpxGfx.fmrb_spx_gfx_draw_rect(@canvas_id, x, y, w, h, color); self; end
  def fill_rect(x, y, w, h, color); FmrbSpxGfx.fmrb_spx_gfx_fill_rect(@canvas_id, x, y, w, h, color); self; end
  def blend_rect(x, y, w, h, color, mode); FmrbSpxGfx.fmrb_spx_gfx_blend_rect(@canvas_id, x, y, w, h, color, mode); self; end
  def draw_circle(x, y, r, color); FmrbSpxGfx.fmrb_spx_gfx_draw_circle(@canvas_id, x, y, r, color); self; end
  def fill_circle(x, y, r, color); FmrbSpxGfx.fmrb_spx_gfx_fill_circle(@canvas_id, x, y, r, color); self; end
  def draw_round_rect(x, y, w, h, r, color); FmrbSpxGfx.fmrb_spx_gfx_draw_round_rect(@canvas_id, x, y, w, h, r, color); self; end
  def fill_round_rect(x, y, w, h, r, color); FmrbSpxGfx.fmrb_spx_gfx_fill_round_rect(@canvas_id, x, y, w, h, r, color); self; end
  def draw_ellipse(x, y, rx, ry, color); FmrbSpxGfx.fmrb_spx_gfx_draw_ellipse(@canvas_id, x, y, rx, ry, color); self; end
  def fill_ellipse(x, y, rx, ry, color); FmrbSpxGfx.fmrb_spx_gfx_fill_ellipse(@canvas_id, x, y, rx, ry, color); self; end
  def draw_triangle(x0, y0, x1, y1, x2, y2, color); FmrbSpxGfx.fmrb_spx_gfx_draw_triangle(@canvas_id, x0, y0, x1, y1, x2, y2, color); self; end
  def fill_triangle(x0, y0, x1, y1, x2, y2, color); FmrbSpxGfx.fmrb_spx_gfx_fill_triangle(@canvas_id, x0, y0, x1, y1, x2, y2, color); self; end
  def draw_arc(x, y, r0, r1, a0, a1, color); FmrbSpxGfx.fmrb_spx_gfx_draw_arc(@canvas_id, x, y, r0, r1, a0, a1, color); self; end
  def fill_arc(x, y, r0, r1, a0, a1, color); FmrbSpxGfx.fmrb_spx_gfx_fill_arc(@canvas_id, x, y, r0, r1, a0, a1, color); self; end

  # ---- text (with font/size caching, like fmrb-gfx.rb) ----
  def set_font(family, size = nil)
    @current_font = size ? [family, size] : [family]
    fam = family == :ja ? 1 : 0
    sz = size || 12
    FmrbSpxGfx.fmrb_spx_gfx_set_font(@canvas_id, fam, sz)
    self
  end

  def set_text_size(size)
    @current_text_size = size
    FmrbSpxGfx.fmrb_spx_gfx_set_text_size(@canvas_id, size)
    self
  end

  def draw_text(x, y, str, color, bg_color = nil, mixed: false)
    s = str.to_s
    flags = 0
    flags |= 1 if bg_color
    flags |= 2 if mixed
    bg = bg_color || 0
    FmrbSpxGfx.fmrb_spx_gfx_draw_text(@canvas_id, x, y, s, s.bytesize, color, bg, flags)
    self
  end

  # Periodic read-outs formatted in C (twins of the mruby Graphics methods), so
  # the desktop's 1Hz repaint allocates nothing. Return true when drawn.
  def draw_wallclock(x, y, color, bg_color)
    FmrbSpxGfx.fmrb_spx_gfx_draw_wallclock(@canvas_id, x, y, color, bg_color) == 1
  end

  def draw_free_iram(x, y, color, bg_color)
    FmrbSpxGfx.fmrb_spx_gfx_draw_free_iram(@canvas_id, x, y, color, bg_color) == 1
  end

  # Positional-argument form of draw_text(..., mixed: true). Keyword arguments
  # allocate a Hash per call, so the no-allocation redraw path (FmrbUI#flush)
  # uses this instead.
  def draw_text_mixed(x, y, str, color, bg_color = nil)
    s = str.to_s
    flags = 2
    flags |= 1 if bg_color
    bg = bg_color || 0
    FmrbSpxGfx.fmrb_spx_gfx_draw_text(@canvas_id, x, y, s, s.bytesize, color, bg, flags)
    self
  end

  # Rendered pixel width of a string with the given (or current) font.
  def text_width(str, family = nil, size = nil)
    key = font_key(family, size)
    metrics = FONT_METRICS[key] || FONT_METRICS[[:default]]
    char_w = metrics[:char_w]
    half_w = metrics[:half_w] || char_w
    kana_w = metrics[:kana_w] || char_w
    # Font0 has no CJK glyphs, so the default font renders a multi-byte run
    # hybrid with misaki_8; count that at 8px to match what is drawn.
    wide_w = (key == [:default]) ? 8 : char_w
    bytes = str.bytes
    width = 0
    i = 0
    n = bytes.length
    while i < n
      b = bytes[i]
      if b < 0x80
        width += half_w
        i += 1
      elsif b < 0xC0
        # Stray continuation byte: skip
        i += 1
      else
        seq_len = if b < 0xE0 then 2
                  elsif b < 0xF0 then 3
                  else 4
                  end
        # Half-width katakana (U+FF61-U+FF9F) is EF BD A1..EF BE 9F and draws
        # in a half cell like ASCII, not a full one.
        b1 = (i + 1 < n) ? bytes[i + 1] : 0
        b2 = (i + 2 < n) ? bytes[i + 2] : 0
        halfkana = (seq_len == 3 && b == 0xEF &&
                    ((b1 == 0xBD && b2 >= 0xA1) || (b1 == 0xBE && b2 <= 0x9F)))
        width += halfkana ? kana_w : wide_w
        i += seq_len
      end
    end
    width * @current_text_size
  end

  def font_height(family = nil, size = nil)
    key = font_key(family, size)
    metrics = FONT_METRICS[key] || FONT_METRICS[[:default]]
    metrics[:line_h] * @current_text_size
  end

  def get_pixel(x, y)
    v = FmrbSpxGfx.fmrb_spx_gfx_get_pixel(@canvas_id, x, y)
    v < 0 ? 0 : v
  end

  # ---- present ----
  def present(x = nil, y = nil)
    if x && y
      FmrbSpxGfx.fmrb_spx_gfx_present(@canvas_id, x, y, 1)
    else
      FmrbSpxGfx.fmrb_spx_gfx_present(@canvas_id, 0, 0, 0)
    end
    self
  end

  # ---- CVBS/NTSC output ----
  def set_output_level(level); FmrbSpxGfx.fmrb_spx_gfx_set_output_level(@canvas_id, level); self; end
  def set_chroma_level(level); FmrbSpxGfx.fmrb_spx_gfx_set_chroma_level(@canvas_id, level); self; end

  # ---- masks ----
  def create_mask(width, height, data)
    d = data.to_s
    FmrbSpxGfx.fmrb_spx_gfx_create_mask(@canvas_id, width, height, d, d.bytesize)
  end

  def delete_mask(mask_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_mask(@canvas_id, mask_id)
    self
  end

  def draw_image_masked(image_id, mask_id, x:, y:)
    FmrbSpxGfx.fmrb_spx_gfx_draw_image_masked(@canvas_id, image_id, mask_id, x, y)
    self
  end

  def draw_tile(image_id, src_x, src_y, w, h, dst_x:, dst_y:)
    FmrbSpxGfx.fmrb_spx_gfx_draw_tile(@canvas_id, image_id, src_x, src_y, w, h, dst_x, dst_y)
    self
  end

  # Thick line (no native primitive; stack parallel 1px lines).
  def draw_thick_line(x0, y0, x1, y1, thickness, color)
    t = thickness.to_i
    if t <= 1
      draw_line(x0, y0, x1, y1, color)
      return self
    end
    dx = x1 - x0
    dy = y1 - y0
    len_sq = dx * dx + dy * dy
    if len_sq == 0
      fill_rect(x0 - t / 2, y0 - t / 2, t, t, color)
      return self
    end
    len = Math.sqrt(len_sq)
    nx = -dy.to_f / len
    ny = dx.to_f / len
    half = (t - 1) / 2
    k = -half
    while k <= (t - 1 - half)
      ox = (nx * k).round
      oy = (ny * k).round
      draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color)
      k += 1
    end
    self
  end

  # ---- files / images ----
  # Transfers only when the graphics-side copy differs (size + CRC32). Use
  # this for assets; checking file_status[:exists] leaves an edited asset
  # stale forever.
  def sync_file(path, dest: nil)
    s = path.to_s
    d = (dest || path).to_s
    FmrbSpxGfx.fmrb_spx_gfx_sync_file(s, s.bytesize, d, d.bytesize) == 1
  end

  def transfer_file(path, dest: nil)
    s = path.to_s
    d = (dest || path).to_s
    FmrbSpxGfx.fmrb_spx_gfx_transfer_file(s, s.bytesize, d, d.bytesize) == 1
  end

  def file_status(path)
    s = path.to_s
    sz = FmrbSpxGfx.fmrb_spx_gfx_file_status(s, s.bytesize)
    sz < 0 ? { exists: false, size: 0 } : { exists: true, size: sz }
  end

  def create_image(path)
    s = path.to_s
    buf = FmrbSpxGfx.fmrb_spx_gfx_create_image_from_file(@canvas_id, s, s.bytesize)
    return nil if buf.bytesize == 0
    { id: SpxBytes.u16(buf, 0), width: SpxBytes.u16(buf, 2), height: SpxBytes.u16(buf, 4) }
  end

  # Play a file of concatenated JPEG frames into this canvas (Modern only).
  # Returns FmrbVideo or nil; nil means this backend cannot play it.
  def video_open(path, x: 0, y: 0, fps: 15, loop: false)
    s = path.to_s
    buf = FmrbSpxGfx.fmrb_spx_gfx_video_open(@canvas_id, s, s.bytesize, x, y,
                                             fps, loop ? 1 : 0)
    return nil if buf.bytesize == 0
    FmrbVideo.new(self, SpxBytes.u16(buf, 0), SpxBytes.u16(buf, 2))
  end

  # Raw hooks FmrbVideo talks through, so the handle class is identical in
  # both engines.
  def _video_control(action)
    buf = FmrbSpxGfx.fmrb_spx_gfx_video_control(action)
    return nil if buf.bytesize == 0
    { state: buf.getbyte(0), shown: SpxBytes.u32(buf, 1), dropped: SpxBytes.u32(buf, 5) }
  end

  def _video_status
    buf = FmrbSpxGfx.fmrb_spx_gfx_video_status
    return nil if buf.bytesize == 0
    { state: buf.getbyte(0), shown: SpxBytes.u32(buf, 1), dropped: SpxBytes.u32(buf, 5) }
  end

  def draw_image(image_id, x: 0, y: 0, scale_x: 1.0, scale_y: 0.0)
    # scale_y_fp8 == 0 is interpreted by the backend as "use scale_x"
    # (uniform), matching gfx.c which just multiplies.
    sxfp = (scale_x * 256.0).to_i
    syfp = (scale_y * 256.0).to_i
    FmrbSpxGfx.fmrb_spx_gfx_draw_image(@canvas_id, image_id, x, y, sxfp, syfp)
    self
  end

  def delete_image(image_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_image(@canvas_id, image_id)
    self
  end

  def load_image(path, coord: nil, mode: nil)
    status = file_status(path)
    unless status[:exists]
      transfer_file(path)
    end
    img = create_image(path)
    return if img.nil?
    img_id = img[:id]
    img_w = img[:width]
    img_h = img[:height]
    if coord == :center
      x = (@canvas_width - img_w) / 2
      y = (@canvas_height - img_h) / 2
    elsif coord
      x = coord[0]
      y = coord[1]
    else
      x = 0
      y = 0
    end
    draw_image(img_id, x: x, y: y)
    present
    delete_image(img_id)
  end

  # ---- composite regions / viewport ----
  def set_composite_regions(regions)
    if regions.nil? || regions.empty?
      FmrbSpxGfx.fmrb_spx_gfx_set_composite_regions(@canvas_id, "", 0)
      return self
    end
    count = regions.length
    buf = "\x00" * (count * 14)
    i = 0
    regions.each do |r|
      dst_x = r[:dst_x] || 0
      dst_y = r[:dst_y] || 0
      src_x = r[:src_x] || dst_x
      src_y = r[:src_y] || dst_y
      w = r[:w] || 0
      h = r[:h] || 0
      trans = r[:transparent] ? 1 : 0
      base = i * 14
      SpxBytes.set_i16(buf, base + 0, src_x)
      SpxBytes.set_i16(buf, base + 2, src_y)
      SpxBytes.set_i16(buf, base + 4, dst_x)
      SpxBytes.set_i16(buf, base + 6, dst_y)
      SpxBytes.set_i16(buf, base + 8, w)
      SpxBytes.set_i16(buf, base + 10, h)
      SpxBytes.set_i16(buf, base + 12, trans)
      i += 1
    end
    FmrbSpxGfx.fmrb_spx_gfx_set_composite_regions(@canvas_id, buf, count)
    self
  end

  def set_viewport(src_x, src_y, w, h)
    FmrbSpxGfx.fmrb_spx_gfx_set_canvas_viewport(@canvas_id, src_x, src_y, w, h)
    self
  end

  def clear_viewport
    FmrbSpxGfx.fmrb_spx_gfx_set_canvas_viewport(@canvas_id, 0, 0, 0, 0)
    self
  end

  # Confine this canvas's sprites to the (x, y, w, h) sub-rect. Sprites are
  # composited above everything the canvas drew, so without this they paint
  # over the window frame the app drew into the same canvas. Coordinates match
  # SpriteInstance#move. FmrbApp sets the user area by default; narrow it
  # further to keep sprites out of an app-drawn status bar.
  def set_sprite_clip(x, y, w, h)
    FmrbSpxGfx.fmrb_spx_gfx_set_sprite_clip(@canvas_id, x, y, w, h)
    self
  end

  def clear_sprite_clip
    FmrbSpxGfx.fmrb_spx_gfx_set_sprite_clip(@canvas_id, 0, 0, 0, 0)
    self
  end

  # ---- sprite low-level (used by SpriteImage / SpriteInstance) ----
  def _create_sprite_image(width, height, trans_color, use_trans)
    FmrbSpxGfx.fmrb_spx_gfx_create_sprite_image(@canvas_id, width, height, trans_color, use_trans)
  end

  def _delete_sprite_image(image_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_sprite_image(@canvas_id, image_id)
    self
  end

  def _load_sprite_image_bmp(image_id, path)
    s = path.to_s
    FmrbSpxGfx.fmrb_spx_gfx_load_sprite_image_bmp(@canvas_id, image_id, s, s.bytesize)
    self
  end

  def _set_sprite_image_target(image_id)
    FmrbSpxGfx.fmrb_spx_gfx_set_sprite_image_target(@canvas_id, image_id)
    self
  end

  def _create_sprite_instance(image_ids, x, y, z_order)
    n = image_ids.length
    buf = "\x00" * (n * 2)
    i = 0
    image_ids.each do |id|
      SpxBytes.set_u16(buf, i * 2, id)
      i += 1
    end
    FmrbSpxGfx.fmrb_spx_gfx_create_sprite_instance(@canvas_id, buf, n, x, y, z_order)
  end

  def _delete_sprite_instance(instance_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_sprite_instance(@canvas_id, instance_id)
    self
  end

  def _sprite_move(instance_id, x, y)
    FmrbSpxGfx.fmrb_spx_gfx_sprite_move(@canvas_id, instance_id, x, y)
    self
  end

  def _sprite_visible(instance_id, visible)
    FmrbSpxGfx.fmrb_spx_gfx_sprite_visible(@canvas_id, instance_id, visible ? 1 : 0)
    self
  end

  def _sprite_frame(instance_id, frame_index)
    FmrbSpxGfx.fmrb_spx_gfx_sprite_frame(@canvas_id, instance_id, frame_index)
    self
  end

  def _delete_all_sprites
    FmrbSpxGfx.fmrb_spx_gfx_delete_all_sprites(@canvas_id)
    self
  end

  # No-op on Spinel: there is no boxed C resource to release (the canvas is
  # owned by the app context and freed in _cleanup).
  def destroy
    nil
  end

  # Color utilities (were FmrbGfx class methods in gfx.c). Pure integer math, so
  # reimplemented in Ruby here rather than crossing the FFI boundary. Kept
  # integer-typed so callers like `LAUNCHER_ICON_SEL = FmrbGfx.rgb_to_332(...)`
  # get an Integer constant (a poly return breaks a `cond ? sel : bg` ternary).
  def self.rgb_to_332(r, g, b)
    r = 0 if r < 0
    r = 255 if r > 255
    g = 0 if g < 0
    g = 255 if g > 255
    b = 0 if b < 0
    b = 255 if b > 255
    ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)
  end

  # h: 0-360, s: 0-255, v: 0-255 -> [r, g, b]
  def self.hsv_to_rgb(h, s, v)
    h = 0 if h < 0
    h = h % 360 if h >= 360
    s = 0 if s < 0
    s = 255 if s > 255
    v = 0 if v < 0
    v = 255 if v > 255
    if s == 0
      return [v, v, v]
    end
    region = h / 60
    remainder = (h - region * 60) * 255 / 60
    p = v * (255 - s) / 255
    q = v * (255 - s * remainder / 255) / 255
    t = v * (255 - s * (255 - remainder) / 255) / 255
    if region == 0
      [v, t, p]
    elsif region == 1
      [q, v, p]
    elsif region == 2
      [p, v, t]
    elsif region == 3
      [p, q, v]
    elsif region == 4
      [t, p, v]
    else
      [v, p, q]
    end
  end

  private

  def font_key(family, size)
    return @current_font if family.nil?
    family == :default ? [:default] : [family, (size || 8)]
  end
end

# GfxBlock: immediate-mode port. The mruby version compiles the drawing block to
# a WROVER program and re-sends only changed registers; the Spinel port simply
# replays the block against the real gfx on every draw. Functionally identical
# (same pixels), without the define-program shim. Slower (re-sends all ops), but
# correct; batching can be added later if profiling shows it matters.
# Handle for a motion-JPEG file being played into a canvas (FmrbGfx#video_open).
# Mirrors the mruby class in picoruby-fmrb-app/mrblib/fmrb-gfx.rb: the numbers
# are the raw protocol values (action 0 play / 1 pause / 2 stop / 3 rewind,
# state 0 idle / 1 playing / 2 paused / 3 finished).
class FmrbVideo
  def initialize(gfx, width, height)
    @gfx = gfx
    @width = width
    @height = height
  end

  def width
    @width
  end

  def height
    @height
  end

  def play
    @gfx._video_control(0)
    self
  end

  def pause
    @gfx._video_control(1)
    self
  end

  def stop
    @gfx._video_control(2)
    self
  end

  def rewind
    @gfx._video_control(3)
    self
  end

  def status
    @gfx._video_status
  end

  def playing?
    st = @gfx._video_status
    st ? st[:state] == 1 : false
  end

  def finished?
    st = @gfx._video_status
    st ? st[:state] == 3 : false
  end
end

class GfxBlock
  # Recorder that forwards the block's DSL calls straight to the gfx instance.
  # Provides the same method names + aliases the mruby Recorder exposes.
  class Recorder
    def initialize(gfx); @g = gfx; end
    def clear(color); @g.clear(color); end
    def fill_rect(x, y, w, h, color); @g.fill_rect(x, y, w, h, color); end
    def draw_rect(x, y, w, h, color); @g.draw_rect(x, y, w, h, color); end
    alias rect draw_rect
    def fill_round_rect(x, y, w, h, r, color); @g.fill_round_rect(x, y, w, h, r, color); end
    def draw_round_rect(x, y, w, h, r, color); @g.draw_round_rect(x, y, w, h, r, color); end
    def draw_line(x0, y0, x1, y1, color); @g.draw_line(x0, y0, x1, y1, color); end
    alias line draw_line
    def fill_circle(x, y, r, color); @g.fill_circle(x, y, r, color); end
    def draw_text(x, y, str, color); @g.draw_text(x, y, str, color); end
    alias text draw_text
  end

  def initialize(gfx, **initial_values, &block)
    @gfx = gfx
    @block = block
    @rec = Recorder.new(gfx)
    @destroyed = false
    # Initial draw with the sample values (mruby issues an initial EXEC too).
    block.call(@rec, **initial_values)
  end

  def draw(**kwargs)
    return if @destroyed
    @block.call(@rec, **kwargs)
    nil
  end

  def destroy
    @destroyed = true
  end

  def destroyed?
    @destroyed
  end
end

# Application framework: FFI reimplementation of app.c (C methods) + fmrb-app.rb
# (Ruby framework). Subclasses override on_create / on_update / on_event / etc.
class FmrbApp
  TITLE_BAR_H = 11
  CORNER_R = 4
  TRANSPARENT_COLOR = 0x01
  SCROLLBAR_W = 10
  SCROLLBAR_BTN_H = 10

  # :gfx matches the mruby base: apps may write gfx.draw_text as well as
  # @gfx.draw_text, and the method form is what sig/ describes.
  attr_reader :name, :running, :window_width, :window_height, :pos_x, :pos_y, :platform, :gfx

  # Same shape as the mruby base: predicates over @_-prefixed internals.
  def fullscreen?; @_fullscreen; end
  def rounded_corners?; @_rounded_corners; end
  def closable?; @_closable; end
  # User area readers, same as the mruby base (FmrbUI needs them).
  attr_reader :user_area_x0, :user_area_y0, :user_area_width, :user_area_height
  # Whether a click on the close-button area may stop the app. System apps
  # that own the screen (the desktop) set this to false: for them the
  # top-right corner is ordinary UI, not a close button. Same as the mruby
  # base.
  def closable=(v); @_closable = v; end

  CLOSE_BTN_CX_OFFSET = 6
  CLOSE_BTN_CY        = 5
  CLOSE_BTN_R         = 3
  CLOSE_BTN_HIT_R     = 5
  CLOSE_BTN_NORMAL_COLOR  = 0xFF
  CLOSE_BTN_PRESSED_COLOR = 0x49

  def initialize
    Log.debug("initialize")
    @running = false
    @_close_btn_pressed = false
    @_closable = true
    @_spin_break = false
    @_timers = []
    @_attached_uis = []

    buf = FmrbSpxApp.fmrb_spx_app_init   # 50-byte snapshot; creates canvas(es)
    @name = SpxBytes.read_name(buf, 0, 32)
    @_fullscreen = buf.getbyte(32) != 0
    @_rounded_corners = buf.getbyte(33) != 0
    @platform = buf.getbyte(34) == 1 ? :esp32 : :linux
    @window_width = SpxBytes.u16(buf, 36)
    @window_height = SpxBytes.u16(buf, 38)
    @pos_x = SpxBytes.u16(buf, 40)
    @pos_y = SpxBytes.u16(buf, 42)
    @_canvas = buf.getbyte(44) != 0 ? SpxBytes.u16(buf, 46) : nil
    @_bg_canvas = buf.getbyte(45) != 0 ? SpxBytes.u16(buf, 48) : nil
    Log.debug("name=#{@name}")

    if @_canvas
      @gfx = FmrbGfx.new(@_canvas, width: @window_width, height: @window_height)
      if @_fullscreen
        @user_area_x0 = 0
        @user_area_y0 = 0
        @user_area_x1 = @window_width
        @user_area_y1 = @window_height
        @user_area_width = @window_width
        @user_area_height = @window_height
      else
        @user_area_x0 = 1
        @user_area_y0 = TITLE_BAR_H
        @user_area_x1 = @window_width - 1
        @user_area_y1 = @window_height - 1
        @user_area_width = @window_width - 2
        @user_area_height = @window_height - TITLE_BAR_H - 1
      end

      if @_bg_canvas
        @bg_gfx = FmrbGfx.new(@_bg_canvas, width: @window_width, height: @window_height)
      else
        @bg_gfx = nil
      end

      _apply_user_area_sprite_clip

      unless @_fullscreen
        draw_window_frame
      end
    else
      @gfx = nil
      @bg_gfx = nil
      Log.debug("Headless app: no graphics initialized")
    end
  end

  def draw_window_frame
    return if @_fullscreen
    return unless @gfx
    saved_font = @gfx.current_font
    saved_size = @gfx.current_text_size
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    _paint_frame
    if saved_font != [:default]
      if saved_font.length == 2
        @gfx.set_font(saved_font[0], saved_font[1])
      else
        @gfx.set_font(saved_font[0])
      end
    end
    @gfx.set_text_size(saved_size) unless saved_size == 1
    _apply_rounded_corner_regions
  end


  # Rows one wheel event asks for, signed: positive scrolls towards the start
  # of a list, which is the wheel pushed away from the user. nil when the
  # event is not a wheel, so an app reads it as
  #
  #   rows = wheel_rows(ev)
  #   if rows
  #     @scroll -= rows
  #     ...
  #
  # How far a notch reaches is the machine's setting (system_conf.toml
  # wheel_lines), not each app's opinion. The scrollbar widget deliberately
  # does NOT answer the wheel: in every app here the bar is a picture of the
  # app's own scroll position, written on each draw, so a value changed inside
  # the widget would be overwritten before it was seen.
  def wheel_rows(ev)
    return nil unless ev[:type] == :mouse_wheel
    d = ev[:delta].to_i
    return nil if d == 0
    d * FmrbConst::WHEEL_LINES
  end


  # The raw notch count, for a list whose rows are not text rows: the
  # launcher's are tiles as tall as several lines, and wheel_lines (a text
  # setting) sends it flying. nil when the event is not a wheel.
  def wheel_notches(ev)
    return nil unless ev[:type] == :mouse_wheel
    d = ev[:delta].to_i
    return nil if d == 0
    d
  end

  # ---- System colours ----
  #
  # The [theme] section of system_conf.toml, as FmrbConst::THEME_*. Apps that
  # show text and controls take their background and ink from here, so one
  # edit to the file restyles the desktop and every app together, and a white
  # page on a Retro NTSC screen can be toned down in one place. A game or a
  # visual piece that owns its whole picture passes explicit colours instead.
  def theme_bg;     FmrbConst::THEME_WINDOW_BG; end   # page background
  def theme_fg;     FmrbConst::THEME_TEXT; end        # ink on theme_bg
  def theme_accent; FmrbConst::THEME_HIGHLIGHT; end   # selection, emphasis
  def theme_border; FmrbConst::THEME_BORDER; end      # rules, boxes, muted text
  def theme_fg_light; FmrbConst::THEME_TEXT_LIGHT; end # ink on accent / button

  # Same default as the mruby base: the theme's window background.
  # Same as the mruby base: wiping the user area includes putting the frame
  # and the attached widgets back. The wipe reaches the rounded corners'
  # outline and the transparent key pixels, and any widget in the wiped area
  # is gone from the screen while FmrbUI still believes it is drawn.
  def clear_user_area(color = FmrbConst::THEME_WINDOW_BG)
    return unless @gfx
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, color)
    # @_bg_canvas is the desktop, which has no frame -- letting it through
    # draws a title bar across the wallpaper. @_fullscreen is refused inside
    # draw_window_frame too; it is named here so the reason is visible.
    draw_window_frame unless @_bg_canvas || @_fullscreen
    _invalidate_attached_uis
    nil
  end

  # FmrbUI calls this on itself when it is created, so clear_user_area can put
  # the widgets back without the app wiring anything up.
  def attach_ui(ui)
    @_attached_uis << ui
    nil
  end


  # The other half of attach_ui, for an app whose window can be resized: the
  # widgets are anchored to the user area at the moment they are built, so a
  # resize means building them again -- and the old set has to go, or
  # clear_user_area keeps repainting it at yesterday's coordinates.
  def detach_ui(ui)
    out = []
    i = 0
    n = @_attached_uis.size
    while i < n
      out << @_attached_uis[i] unless @_attached_uis[i] == ui
      i += 1
    end
    @_attached_uis = out
    nil
  end

  def _invalidate_attached_uis
    n = @_attached_uis.size
    i = 0
    while i < n
      @_attached_uis[i].invalidate_all
      i += 1
    end
    nil
  end

  private

  # Draw the window frame directly (immediate mode). The mruby build used a
  # cached GfxBlock program here, but Spinel cannot store a proc that captures
  # outer locals ("later slice"), so the base class draws frame/scrollbar
  # inline instead. Functionally identical; re-issues all ops each redraw.
  def _paint_frame
    return unless @gfx
    g = @gfx
    w = @window_width
    h = @window_height
    # The frame is the system's, not the app's, so it takes the system's
    # colours -- the same four the mruby base uses (fmrb-app.rb). The values
    # it used to carry (0xC5 and 0x60) were the light theme's menu_bg and border
    # spelled out, which is why a changed theme left every title bar behind.
    bar = FmrbConst::THEME_MENU_BG
    on_bar = FmrbConst::THEME_TEXT_LIGHT
    # Title bar with rounded top corners.
    g.fill_round_rect(0, 0, w, TITLE_BAR_H, CORNER_R, bar)
    g.fill_rect(0, CORNER_R, w, TITLE_BAR_H - CORNER_R, bar)
    # Menu button (hamburger) + title text.
    g.fill_rect(3, 3, 9, 1, on_bar)
    g.fill_rect(3, 5, 9, 1, on_bar)
    g.fill_rect(3, 7, 9, 1, on_bar)
    g.draw_text(15, 2, @name, on_bar)
    # Close button + rounded border.
    g.fill_circle(w - 6, 5, 3, on_bar)
    g.draw_round_rect(0, 0, w, h, CORNER_R, FmrbConst::THEME_BORDER)
    # Re-stamp the outer corner pixels with the canvas color key so the rounded
    # corners composite as transparent again after clears/resizes.
    t = TRANSPARENT_COLOR
    g.draw_line(0, 0, 1, 0, t)
    g.draw_line(0, 1, 0, 1, t)
    g.draw_line(w - 2, 0, w - 1, 0, t)
    g.draw_line(w - 1, 1, w - 1, 1, t)
    g.draw_line(0, h - 2, 0, h - 1, t)
    g.draw_line(1, h - 1, 1, h - 1, t)
    g.draw_line(w - 1, h - 2, w - 1, h - 1, t)
    g.draw_line(w - 2, h - 1, w - 2, h - 1, t)
  end

  def _apply_rounded_corner_regions
    return if @_fullscreen
    return unless @gfx
    return unless @_rounded_corners
    return if @_bg_canvas
    w = @window_width
    h = @window_height
    return if @_composite_region_w == w && @_composite_region_h == h
    c = CORNER_R
    @gfx.set_composite_regions([
      { dst_x: 0,     dst_y: 0,     w: c,         h: c,         transparent: true  },
      { dst_x: w - c, dst_y: 0,     w: c,         h: c,         transparent: true  },
      { dst_x: 0,     dst_y: h - c, w: c,         h: c,         transparent: true  },
      { dst_x: w - c, dst_y: h - c, w: c,         h: c,         transparent: true  },
      { dst_x: c,     dst_y: 0,     w: w - 2 * c, h: c,         transparent: false },
      { dst_x: c,     dst_y: h - c, w: w - 2 * c, h: c,         transparent: false },
      { dst_x: 0,     dst_y: c,     w: w,         h: h - 2 * c, transparent: false },
    ])
    @_composite_region_w = w
    @_composite_region_h = h
  end

  # Sprites are composited above everything the canvas drew, frame included,
  # so a windowed app's sprites would paint over its own title bar and border.
  # Bound them to the user area by default; apps wanting a narrower area (e.g.
  # keeping a playfield out of their own score bar) call set_sprite_clip after
  # startup. Re-applied on resize, which drops the clip on the backend.
  def _apply_user_area_sprite_clip
    return unless @gfx
    # system_desktop (the only app with a bg_canvas) draws no window frame and
    # places its own sprites - menu bar indicators, launcher icons - across the
    # whole canvas, so there is nothing to protect and a user-area rect would
    # only cut them.
    return if @_bg_canvas
    if @_fullscreen
      @gfx.clear_sprite_clip
    else
      @gfx.set_sprite_clip(@user_area_x0, @user_area_y0,
                           @user_area_width, @user_area_height)
    end
  end

  public



  # ---- modifier key helpers ----
  def ev_ctrl?(ev);  ((ev[:modifier] || 0) & 0x0C) != 0; end
  def ev_shift?(ev); ((ev[:modifier] || 0) & 0x03) != 0; end
  def ev_alt?(ev);   ((ev[:modifier] || 0) & 0x30) != 0; end

  # ---- lifecycle methods (override in subclass) ----
  def on_create; Log.debug("on_create"); end
  def on_update; 330; end
  def on_destroy; Log.debug("on_destroy"); end
  def on_suspend; Log.debug("on_suspend"); end
  def on_resume; Log.debug("on_resume"); end
  def on_resize(new_width, new_height); end
  # Default no-op so the Spinel build can statically resolve the call in
  # _dispatch_control (mruby relied on respond_to? + dynamic dispatch; Spinel
  # cannot compile a call to a method that is defined nowhere). Subclasses
  # override to handle custom APP_CONTROL commands.
  def on_control(msg); nil; end

  # The frame's own event handling; see the mruby base for the rationale.
  # This file is a separate implementation, so the same split exists twice.
  def _frame_event(ev)
    if @_closable && ev[:button] == 1 && (ev[:type] == :mouse_down || ev[:type] == :mouse_up)
      cx = @window_width - CLOSE_BTN_CX_OFFSET
      cy = CLOSE_BTN_CY
      hit = (ev[:x] - cx).abs <= CLOSE_BTN_HIT_R && (ev[:y] - cy).abs <= CLOSE_BTN_HIT_R
      case ev[:type]
      when :mouse_down
        if hit && !@_fullscreen && @gfx
          @_close_btn_pressed = true
          @gfx.fill_circle(cx, cy, CLOSE_BTN_R, CLOSE_BTN_PRESSED_COLOR)
          @gfx.present
        end
      when :mouse_up
        if @_close_btn_pressed
          @_close_btn_pressed = false
          if hit
            stop
          elsif @gfx
            # The frame drew this circle in THEME_TEXT_LIGHT, so putting it
            # back in CLOSE_BTN_NORMAL_COLOR (white) changed its colour
            # under any theme whose light ink is not white.
            @gfx.fill_circle(cx, cy, CLOSE_BTN_R, FmrbConst::THEME_TEXT_LIGHT)
            @gfx.present
          end
        elsif hit && !@_fullscreen && @gfx
          # Safety net for a missed down event. It needs the same guards as
          # the down path: without them a plain click on the top-right corner
          # of a fullscreen app (which draws no close button at all) silently
          # stopped it.
          stop
        end
      end
    end
    # A fullscreen app has no title bar -- y < 11 is just the top of its
    # picture -- so the reload needs the guard the close button always had.
    if ev[:type] == :mouse_up && ev[:button] == 3 && ev[:y] < 11 && !@_fullscreen
      request_reload if _is_file_app
    end
  end

  # Frame first, then the app's hook: the "call super(ev)" contract is gone.
  def _dispatch_event(ev)
    _frame_event(ev)
    on_event(ev)
  end

  def on_event(ev)
    # App hook, overridden freely; super(ev) from existing apps is a no-op.
  end

  def request_reload
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "reload_confirm" })
  end

  # ---- main loop (poll form; C _spin's dispatch is reimplemented below) ----
  def main_loop
    Log.debug("main_loop started")
    @_suspended = false
    loop do
      return if !@running
      if @_suspended
        _spin(500)
        next
      end
      timeout_ms = on_update
      _spin(timeout_ms)
    end
  end

  # Poll messages up to timeout_ms and dispatch them, then run due timers.
  # Replaces FmrbApp#_spin (which dispatched via mrb_funcall in C).
  def _spin(timeout_ms)
    target = Machine.board_millis + timeout_ms
    # Cleared on entry so only a request made from inside this wait counts;
    # one made from on_update is about to be answered by the next sleep.
    @_spin_break = false
    loop do
      now = Machine.board_millis
      break if now >= target
      remaining = target - now
      msg = _poll_message(remaining)
      break if msg.nil?
      _dispatch_message(msg)
      break if @_spin_break
    end
    _run_timers
    nil
  end

  # Same contract as FmrbApp#request_early_update in the mruby base: end this
  # wait now so on_update runs and can act on a deadline the callback set.
  def request_early_update
    @_spin_break = true
    nil
  end

  # { type:, src_pid:, data: } or nil on timeout.
  def _poll_message(timeout_ms)
    data = FmrbSpxApp.fmrb_spx_app_recv_message(timeout_ms, FmrbSpxApp.type_out, FmrbSpxApp.src_out)
    type = FmrbSpxApp.read_i32(FmrbSpxApp.type_out)
    return nil if type < 0
    src = FmrbSpxApp.read_i32(FmrbSpxApp.src_out)
    { type: type, src_pid: src, data: data }
  end

  def _dispatch_message(msg)
    t = msg[:type]
    if t == FmrbConst::MSG_TYPE_HID_EVENT
      ev = _parse_hid_event(msg[:data])
      _dispatch_event(ev) if ev
    elsif t == FmrbConst::MSG_TYPE_APP_CONTROL
      _dispatch_control(msg[:data])
    end
  end

  # Parse a raw HID event payload into the same symbol-keyed Hash the mruby C
  # dispatcher built (fmrb_hid_msg.h layouts). Returns nil for unknown/short.
  def _parse_hid_event(data)
    return nil if data.bytesize < 1
    subtype = data.getbyte(0)
    case subtype
    when 1, 2  # KEY_DOWN / KEY_UP
      return nil if data.bytesize < 5
      { type: (subtype == 1 ? :key_down : :key_up),
        keycode: data.getbyte(1),
        scancode: data.getbyte(2),
        modifier: data.getbyte(3),
        character: data.getbyte(4) }
    when 4, 5  # MOUSE_BUTTON_DOWN / UP
      return nil if data.bytesize < 6
      { type: (subtype == 4 ? :mouse_down : :mouse_up),
        button: data.getbyte(1),
        x: SpxBytes.u16(data, 2),
        y: SpxBytes.u16(data, 4) }
    when 3  # MOUSE_MOVE (kernel sends 6-byte [subtype,button,x_lo,x_hi,y_lo,y_hi])
      return nil if data.bytesize < 6
      { type: :mouse_move,
        x: SpxBytes.u16(data, 2),
        y: SpxBytes.u16(data, 4) }
    when 10  # MOUSE_WHEEL (notches in the button slot, signed)
      return nil if data.bytesize < 6
      d = data.getbyte(1)
      d -= 256 if d > 127
      { type: :mouse_wheel,
        delta: d,
        x: SpxBytes.u16(data, 2),
        y: SpxBytes.u16(data, 4) }
    when 6, 7  # GAMEPAD_BUTTON_DOWN / UP
      return nil if data.bytesize < 3
      { type: (subtype == 6 ? :gamepad_down : :gamepad_up),
        gamepad_id: data.getbyte(1),
        button: data.getbyte(2) }
    when 9  # KANA_MODE (JP layout: kana input turned on/off or switched)
      return nil if data.bytesize < 2
      { type: :kana_mode, mode: data.getbyte(1) }
    when 8  # GAMEPAD_AXIS
      return nil if data.bytesize < 5
      { type: :gamepad_axis,
        gamepad_id: data.getbyte(1),
        axis: data.getbyte(2),
        value: SpxBytes.i16(data, 3) }
    else
      nil
    end
  end

  def _dispatch_control(data)
    h = MessagePack.unpack(data)
    return unless h.is_a?(Hash)
    cmd = h["cmd"]
    return unless cmd.is_a?(String)
    if cmd == "resize"
      w = h["width"]
      ht = h["height"]
      if w.is_a?(Integer) && ht.is_a?(Integer)
        # A runtime window <-> fullscreen switch carries the new mode (P2's
        # fmrb_app_set_fullscreen); a plain resize omits it and stays windowed.
        # The user area follows: fullscreen has no title bar and no border.
        fs = h["fullscreen"]
        @_fullscreen = (fs == true) unless fs.nil?
        @window_width = w
        @window_height = ht
        if @_fullscreen
          @user_area_x0 = 0
          @user_area_y0 = 0
          @user_area_width = w
          @user_area_height = ht
          @user_area_x1 = w
          @user_area_y1 = ht
        else
          @user_area_x0 = 1
          @user_area_y0 = TITLE_BAR_H
          @user_area_width = w - 2
          @user_area_height = ht - TITLE_BAR_H - 1
          @user_area_x1 = w - 1
          @user_area_y1 = ht - 1
        end
        # The backend drops the sprite clip on resize (it was sized for the old
        # active area), so re-issue one for the new user area.
        _apply_user_area_sprite_clip
        on_resize(w, ht)
      end
    elsif cmd == "quit_request"
      _handle_system_control(h)
    elsif cmd == "suspend" || cmd == "resume" || cmd == "stop" || cmd == "clear_and_stop"
      _handle_system_control(h)
    else
      on_control(h)
    end
  end

  def _handle_system_control(msg)
    case msg["cmd"]
    when "suspend"
      @_suspended = true
      on_suspend
      Log.info("App #{@name} suspended")
    when "resume"
      @_suspended = false
      on_resume
      Log.info("App #{@name} resumed")
    when "stop"
      Log.info("App #{@name} received stop command")
      stop
    when "clear_and_stop"
      Log.info("App #{@name} clearing canvas and stopping")
      if @gfx
        @gfx.clear(0x00)
        @gfx.present
      end
      stop
    when "quit_request"
      # Ctrl+Q on a built-in app: the kernel asks instead of stopping it, so an
      # app holding unsaved state can put a question to the user first (P2).
      on_quit_request
    end
  end

  # Ctrl+Q. Override to confirm before closing; the default is to close.
  def on_quit_request
    Log.info("App #{@name} quit request")
    stop
  end

  # ---- timers (Ruby-side; C cannot call a Ruby block) ----
  # NOTE: the mruby FmrbApp#set_timer was a non-functional stub. This gives a
  # working implementation; when unused, @_timers stays empty and _run_timers
  # is a no-op, so no behavior diverges for apps that never call set_timer.
  def set_timer(interval, &blk)
    id = (@_timer_seq ||= 0) + 1
    @_timer_seq = id
    @_timers << { id: id, at: Machine.board_millis + interval, interval: interval, blk: blk }
    id
  end

  def clear_time(timer_id)
    @_timers.reject! { |t| t[:id] == timer_id } if @_timers
    nil
  end

  # Runs on every turn of the app loop, so it is written to cost nothing when
  # nothing is due: no blocks (passing one costs ~0.4 ms here) and no arrays
  # until there is actually a timer to fire.
  #
  # The split is explicit rather than `due, rest = ...partition` -- Spinel
  # cannot multi-assign from a partition on a poly (Hash-element) array.
  def _run_timers
    timers = @_timers
    return if timers.nil?
    count = timers.size
    return if count == 0

    now = Machine.board_millis
    i = 0
    due_count = 0
    while i < count
      due_count += 1 if timers[i][:at] <= now
      i += 1
    end
    return if due_count == 0

    due = []
    keep = []
    i = 0
    while i < count
      t = timers[i]
      if t[:at] <= now
        due << t
      else
        keep << t
      end
      i += 1
    end
    # Swap the list in before running anything: a callback that arms a new
    # timer has to land in the list that survives, not in the one being
    # replaced.
    @_timers = keep
    i = 0
    while i < due.size
      blk = due[i][:blk]
      blk.call if blk
      i += 1
    end
  end

  def subscribe(topic)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "subscribe", "topic" => topic })
  end

  def unsubscribe(topic)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "unsubscribe", "topic" => topic })
  end

  def publish(topic, data = nil)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "publish", "topic" => topic, "data" => data })
  end

  def request_file_select(mode = "open")
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "file_select", "mode" => mode })
  end

  # Run a file, replacing the instance this app started last time (editor F5).
  def request_run(path, prev_pid = nil)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "run", "path" => path, "prev_pid" => prev_pid })
  end

  # Ask the kernel to switch this app between windowed and fullscreen (P2). The
  # VM keeps running; the answer arrives as on_resize with @_fullscreen and the
  # user area already updated.
  def request_fullscreen(on)
    cmd = on ? "enter_fullscreen" : "exit_fullscreen"
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => cmd })
  end

  def toggle_fullscreen
    request_fullscreen(!@_fullscreen)
  end

  def send_message(dest_pid, msg_type, data)
    binary_data = MessagePack.pack(data)
    _send_message(dest_pid, msg_type, binary_data)
  end

  def set_window_position(x, y)
    _set_window_param(:pos_x, x)
    _set_window_param(:pos_y, y)
    @gfx.present if @gfx
    self
  end

  def create_canvas_gfx(width:, height:, z_offset: 1, transparent: false, transparent_color: 0)
    id = _create_canvas(width, height, z_offset, transparent ? 1 : 0, transparent_color)
    FmrbGfx.new(id, width: width, height: height)
  end

  def delete_canvas_gfx(gfx)
    _delete_canvas(gfx.canvas_id)
  end

  def destroy
    Log.debug("destroy() called")
    begin
      # Same as the mruby base: reaching destroy means stop was called.
      exit_data = MessagePack.pack({ "cmd" => "exit", "expected" => true })
      _send_message(0, FmrbConst::MSG_TYPE_APP_CONTROL, exit_data)
    rescue => e
      Log.error("Failed to send exit notification: #{e}")
    end
    if @gfx
      @gfx.destroy
      @gfx = nil
    end
    on_destroy
    _cleanup
  end

  def start
    Log.debug("start() called")
    @running = true
    on_create
    main_loop
    destroy
  end

  # Same contract as FmrbApp#stop in the mruby base: every intended end goes
  # through here, and marking it is what lets the kernel tell an app that was
  # asked to stop from one that died.
  def stop
    @running = false
    FmrbSpxApp.fmrb_spx_app_mark_expected_stop
    nil
  end

  # ---- instance FFI wrappers (were C methods in app.c) ----
  def _send_message(dest_pid, msg_type, data)
    s = data.to_s
    FmrbSpxApp.fmrb_spx_app_send_message(dest_pid, msg_type, s, s.bytesize) == 1
  end

  # FmrbAudio#note_on / note_off go through this. It exists here because the
  # mruby build resolves the respond_to? check in FmrbAudio at run time and
  # Spinel resolves it statically: without the method the compiled call is
  # still emitted and the app dies with NoMethodError the first time it plays
  # a note.
  def _send_audio_note(on, ch, freq, vol, duty, sweep)
    flag = on ? 1 : 0
    FmrbSpxApp.fmrb_spx_app_send_audio_note(flag, ch.to_i, freq.to_i, vol.to_i,
                                            duty.to_i, sweep.to_i) == 1
  end

  def _set_window_param(param, value)
    which = param == :pos_y ? 1 : 0
    FmrbSpxApp.fmrb_spx_app_set_window_param(which, value)
    self
  end

  def _is_file_app
    FmrbSpxApp.fmrb_spx_app_is_file_app == 1
  end

  def _create_canvas(w, h, z_offset, use_transparent, transparent_color)
    FmrbSpxApp.fmrb_spx_app_create_canvas(w, h, z_offset, use_transparent, transparent_color)
  end

  def _delete_canvas(canvas_id)
    FmrbSpxApp.fmrb_spx_app_delete_canvas(canvas_id)
    nil
  end

  def _cleanup
    FmrbSpxApp.fmrb_spx_app_cleanup
    nil
  end

  # Collect while this app waits in _spin (FmrbApp#idle_gc= in the mruby base).
  #
  # There is nothing to turn on here: Spinel's collector is not incremental
  # and has no scheduler-driven mode or step limit, so it never runs inside
  # _spin. The setter is kept so an app that asks for it (system_desktop does)
  # compiles and behaves the same on both engines, and #idle_gc answers
  # honestly with false. Apps that need to reclaim a burst call FmrbApp.gc.
  def idle_gc=(enable)
    @_idle_gc = false
    nil
  end

  def idle_gc
    false
  end

  # Force a full GC on this task's Spinel heap.
  def self.gc
    FmrbSpxApp.fmrb_spx_app_gc
  end

  # ---- class methods (were C class methods; parse packed :binstr records) ----
  def self.ps
    buf = FmrbSpxApp.fmrb_spx_app_ps
    # FMRB_SPX_APP_PS_RECORD_SIZE (fmrb_spx_app.h)
    count = buf.bytesize / 64
    list = []
    i = 0
    while i < count
      b = i * 64
      list << {
        id: buf.getbyte(b + 0),
        state: buf.getbyte(b + 1),
        type: buf.getbyte(b + 2),
        vm_type: buf.getbyte(b + 3),
        gen: SpxBytes.u32(buf, b + 4),
        stack_water: SpxBytes.u32(buf, b + 8),
        mem_total: SpxBytes.u32(buf, b + 12),
        mem_used: SpxBytes.u32(buf, b + 16),
        mem_free: SpxBytes.u32(buf, b + 20),
        mem_frag: SpxBytes.i32(buf, b + 24),
        name: SpxBytes.read_name(buf, b + 28, 32),
        headless: buf.getbyte(b + 60) != 0,
      }
      i += 1
    end
    list
  end

  def self.heap_info
    buf = FmrbSpxApp.fmrb_spx_app_heap_info
    {
      free: SpxBytes.u32(buf, 0),
      total: SpxBytes.u32(buf, 4),
      min_free: SpxBytes.u32(buf, 8),
      largest_block: SpxBytes.u32(buf, 12),
      iram_free: SpxBytes.u32(buf, 16),
      iram_total: SpxBytes.u32(buf, 20),
    }
  end

  # Percent of this VM's pool in use (-1 when unavailable), matching the mruby
  # FmrbApp.pool_usage so app-side logging reads the same on both engines.
  def self.pool_usage
    FmrbSpxApp.fmrb_spx_app_pool_usage
  end

  # Bytes of this VM's pool in use (-1 when unavailable), matching the mruby
  # FmrbApp.pool_used. The launcher's rescan logs the difference across a
  # scan, which a percent cannot show.
  def self.pool_used
    FmrbSpxApp.fmrb_spx_app_pool_used
  end

  def self.sys_pool_info
    buf = FmrbSpxApp.fmrb_spx_app_sys_pool_info
    {
      total: SpxBytes.u32(buf, 0),
      used: SpxBytes.u32(buf, 4),
      free: SpxBytes.u32(buf, 8),
      used_blocks: SpxBytes.u32(buf, 12),
      free_blocks: SpxBytes.u32(buf, 16),
    }
  end

  def self.gfx_stats
    buf = FmrbSpxApp.fmrb_spx_app_gfx_stats
    { cmds: SpxBytes.u32(buf, 0), presents: SpxBytes.u32(buf, 4) }
  end

  def self._get_last_error
    buf = FmrbSpxApp.fmrb_spx_app_last_error
    return nil if buf.bytesize == 0
    { name: SpxBytes.read_name(buf, 0, 64), error: SpxBytes.read_name(buf, 64, 112) }
  end

  def self.config(section)
    s = section.to_s
    buf = FmrbSpxApp.fmrb_spx_app_config(s, s.bytesize)
    return nil if buf.bytesize == 0
    off = 0
    table_count = buf.getbyte(off)
    off += 1
    tables = []
    t = 0
    while t < table_count
      kv_count = buf.getbyte(off)
      off += 1
      h = {}
      k = 0
      while k < kv_count
        klen = buf.getbyte(off)
        off += 1
        key = SpxBytes.slice(buf, off, klen)
        off += klen
        vlen = SpxBytes.u16(buf, off)
        off += 2
        val = SpxBytes.slice(buf, off, vlen)
        off += vlen
        h[key] = val
        k += 1
      end
      tables << h
      t += 1
    end
    tables
  end

  # UI language ("en" / "ja"). Read at run time rather than from FmrbConst:
  # that table is generated when the Spinel program is compiled, and the
  # language is a setting the Config app changes.
  def self.language
    FmrbSpxApp.fmrb_spx_app_language.to_s
  end

  # Kana input mode: 0 = off, 1 = hiragana, 2 = katakana. Behind the
  # clickable mode indicators.
  def self.set_kana_mode(mode)
    FmrbSpxApp.fmrb_spx_app_set_kana_mode(mode)
    nil
  end

  # Remote desktop video going out: 0 = none, 1 = MJPEG, 2 = H.264.
  def self.rd_stream_state
    FmrbSpxApp.fmrb_spx_app_rd_stream_state
  end

  def self.wallclock
    buf = FmrbSpxApp.fmrb_spx_app_wallclock
    return nil if buf.bytesize == 0
    _decode_wallclock(buf)
  end

  def self.set_wallclock(year, month, day, hour, minute, second)
    buf = FmrbSpxApp.fmrb_spx_app_set_wallclock(year, month, day, hour, minute, second)
    return nil if buf.bytesize == 0
    _decode_wallclock(buf)
  end

  def self._decode_wallclock(buf)
    {
      year: SpxBytes.u16(buf, 0),
      month: SpxBytes.u16(buf, 2),
      day: SpxBytes.u16(buf, 4),
      hour: SpxBytes.u16(buf, 6),
      minute: SpxBytes.u16(buf, 8),
      second: SpxBytes.u16(buf, 10),
    }
  end

  def self.enable_cursor
    FmrbSpxApp.fmrb_spx_app_enable_cursor
    nil
  end

  def self.set_cursor_visible(visible)
    FmrbSpxApp.fmrb_spx_app_set_cursor_visible(visible ? 1 : 0)
    nil
  end

  def self.reboot
    FmrbSpxApp.fmrb_spx_app_reboot
    nil
  end

  def self.ble_start
    FmrbSpxApp.fmrb_spx_app_ble_start != 0
  end

  # BLE host UI state, 0 = off (same source as the mruby binding).
  def self.ble_state
    FmrbSpxApp.fmrb_spx_app_ble_state
  end

  def self.wifi_connected?
    FmrbSpxApp.fmrb_spx_app_wifi_connected == 1
  end

  # Process-set generation: bumped by the kernel on every state transition, so
  # a 1Hz poll can skip the allocating ps call while nothing changed.
  def self.ps_gen
    FmrbSpxApp.fmrb_spx_app_proc_generation
  end

  def self.wifi_info
    buf = FmrbSpxApp.fmrb_spx_app_wifi_info
    return nil if buf.bytesize == 0
    {
      connected: buf.getbyte(0) != 0,
      ip: SpxBytes.read_name(buf, 1, 16),
      ssid: SpxBytes.read_name(buf, 17, 33),
      hostname: SpxBytes.read_name(buf, 50, 32),
    }
  end

  def self._clear_cache(path)
    s = path.to_s
    buf = FmrbSpxApp.fmrb_spx_app_clear_cache(s, s.bytesize)
    {
      ok: buf.getbyte(0) != 0,
      deleted: SpxBytes.u32(buf, 1),
      status: SpxBytes.i32(buf, 5),
    }
  end

  def self.usb_devices
    buf = FmrbSpxApp.fmrb_spx_app_usb_devices
    count = buf.bytesize / 10
    list = []
    i = 0
    while i < count
      b = i * 10
      list << {
        type: _usb_type_str(buf.getbyte(b + 0)),
        layout_valid: buf.getbyte(b + 1) != 0,
        vid: SpxBytes.u16(buf, b + 2),
        pid: SpxBytes.u16(buf, b + 4),
        addr: buf.getbyte(b + 6),
        slot: buf.getbyte(b + 7),
        report_len: SpxBytes.u16(buf, b + 8),
      }
      i += 1
    end
    list
  end

  def self._usb_type_str(code)
    case code
    when 1 then "KBD"
    when 2 then "MOUSE"
    when 3 then "GAMEPAD"
    else "OTHER"
    end
  end

  def self.hid_raw_subscribe(slot)
    FmrbSpxApp.fmrb_spx_app_hid_raw_subscribe(slot) == 1
  end

  def self.hid_raw_unsubscribe(slot)
    FmrbSpxApp.fmrb_spx_app_hid_raw_unsubscribe(slot) == 1
  end
end

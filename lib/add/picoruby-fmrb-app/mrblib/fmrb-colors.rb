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

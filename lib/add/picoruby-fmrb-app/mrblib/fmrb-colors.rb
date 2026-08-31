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
#   bg = 0x24        # RGB332, as in system_conf.toml
#   text = 0xFF
#   [shell]
#   bg = 0x00
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

  # { "bg" => 0x24, ... } for one section; empty when there is nothing to say.
  def self.section(name)
    out = {}
    text = nil
    begin
      f = File.open(PATH, "r")
      text = f.read
      f.close
    rescue => e
      return out
    end
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

  # "0x24" or "36" -> 36. nil for anything else, so a typo leaves the theme
  # colour in place instead of painting the app black.
  def self.to_color(val)
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
      n & 0xFF
    else
      i = 0
      while i < val.length
        return nil if HEX.index(val[i, 1]).nil? || val[i, 1] > "9"
        i += 1
      end
      return nil if val.length == 0
      val.to_i & 0xFF
    end
  end
end

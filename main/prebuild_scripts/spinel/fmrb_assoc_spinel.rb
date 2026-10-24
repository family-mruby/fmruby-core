# FmrbAssoc's table reader for the Spinel desktop (the mruby one is
# lib/add/picoruby-fmrb-app/mrblib/fmrb-assoc-file.rb, through FmrbToml).
#
# Spinel programs cannot call the FmrbToml binding (an mrbgem C method), so
# the Spinel desktop keeps this line reader: one "ext = value" per line, a
# line that does not parse is skipped. Listed after fmrb-assoc.rb in
# tool/spinel/gen_app_combined.rb.
module FmrbAssoc
  def self.merge_file(table, path)
    text = read_file(path)
    return nil unless text
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.empty?
      next if line.start_with?("#")
      eq = line.index("=")
      next unless eq
      next if eq == 0
      key = line[0, eq].strip.downcase
      next if key.empty?
      val = unquote(line[eq + 1, line.length - eq - 1].strip)
      next if val.empty?
      table[key] = val
    end
    nil
  end

  def self.unquote(raw)
    s = raw.to_s
    # Anything after a # is a comment, but only outside the quotes.
    if s.start_with?("\"")
      body = s[1, s.length - 1]
      close = body.index("\"")
      return close ? body[0, close] : body
    end
    hash = s.index("#")
    s = s[0, hash] if hash
    s.strip
  end

  def self.read_file(path)
    f = nil
    begin
      # Bare File, not ::File: Spinel cannot compile a call whose receiver is a
      # constant path (`unsupported call: node ... recv=ConstantPathNode`), and
      # this file is compiled into the desktop. The rest of the machine spells
      # it the same way for the same reason (nsf-header.rb, config_dialog.rb).
      f = File.open(path, "r")
      text = f.read
      f.close
      f = nil
      text
    rescue => e
      # Missing is ordinary: a machine with no user table has no
      # /home/associations.toml, and that is not worth a line in the log.
      begin
        f.close if f
      rescue
      end
      nil
    end
  end
end

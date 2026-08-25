# Which app opens which file (doc/user_extension/assoc).
#
# One table, consulted by everything that opens a file on the user's behalf --
# the file manager's double click, the shell's `open`, and any app that wants
# to hand a file on. Keeping the answer in one place is the point: without it
# each of them grows its own list of extensions and they drift.
#
#   FmrbAssoc.resolve("/home/slides/demo.md")  #=> "/app/tool/picorabbit.app.rb"
#   FmrbAssoc.resolve("/home/sample/hello.rb") #=> "run"
#   FmrbAssoc.resolve("/home/notes.txt")       #=> "edit"
#
# The answer is one of:
#   "run"        spawn the file itself as an app (it is a program)
#   "edit"       open it in the editor
#   "/app/..."   spawn that app and hand it the file
#
# A path always begins with "/", so a caller tells the three apart by
# comparing against RUN and EDIT; nothing else is needed.
#
# The tables are two layers, the same shape as everywhere else on this
# machine: /etc/associations.toml ships with the firmware, /home/associations
# .toml is the user's and wins per extension. The format is one line per
# extension and nothing else:
#
#   md  = "/app/tool/picorabbit.app.rb"
#   rb  = "run"
#   txt = "edit"
#
# Read once and kept. Editing a table takes effect the next time the app that
# reads it starts -- there is no reload, because the alternative is every
# consumer polling a file it almost never has to.
module FmrbAssoc
  SYS_PATH = "/etc/associations.toml"
  USR_PATH = "/home/associations.toml"

  RUN = "run"
  EDIT = "edit"

  # What an extension with no entry gets. The editor opens anything, and
  # showing a file is a better answer to "I do not know" than doing nothing.
  DEFAULT = EDIT

  # Longest extension worth looking at. Bounds the work done on a name that
  # happens to contain a lot of dots.
  MAX_EXT_LEN = 8

  def self.resolve(path)
    ext = extension_of(path)
    return DEFAULT if ext.empty?
    table = load_table
    v = table[ext]
    return DEFAULT unless v
    v
  end

  # The part after the last dot, lower case, without the dot. "" when there is
  # no extension, when the dot is the last character, or when what follows is
  # too long to be one.
  #
  # Lower case because a table is written in lower case and a file called
  # README.MD is the same kind of file as readme.md. downcase is used on the
  # extension alone, which is a handful of characters.
  def self.extension_of(path)
    s = path.to_s
    dot = s.rindex(".")
    return "" unless dot
    slash = s.rindex("/")
    # A dot in a directory name is not this file's extension.
    return "" if slash && dot < slash
    len = s.length - dot - 1
    return "" if len <= 0 || len > MAX_EXT_LEN
    s[dot + 1, len].downcase
  end

  # System table first, user table on top of it. Read once per VM.
  def self.load_table
    t = @table
    return t if t
    t = {}
    merge_file(t, SYS_PATH)
    merge_file(t, USR_PATH)
    @table = t
    t
  end

  # For a caller that has just written a table and wants it now (and for the
  # host tests). Not wired to anything: see the note about reloading above.
  def self.forget
    @table = nil
    nil
  end

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

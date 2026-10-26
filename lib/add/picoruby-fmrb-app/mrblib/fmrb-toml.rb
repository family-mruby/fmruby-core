# TOML for the machine's Ruby (doc/archive/app_refactor R2).
#
# Reading is the firmware's own TOML reader (components/fmrb_toml, tomlc99,
# the one that reads system_conf.toml and every .app.toml), so a file means
# the same thing to Ruby as to the system:
#
#   FmrbToml.load("/home/walk_scroller.toml")  #=> {"node" => "", "mark" => [{...}, ...]}
#   FmrbToml.parse("a = 1\n[s]\nb = \"x\"\n")   #=> {"a" => 1, "s" => {"b" => "x"}}
#
# Tables are Hash, arrays Array, [[name]] an Array of Hash; strings, integers
# (0x.. too), floats and true/false as themselves. A date, or a value of one
# unquoted word (`bg = black`), comes back as the text written.
#
# A file that is not TOML -- one bad line is enough -- reads as no file at
# all: load answers nil and says why in one log line (FmrbToml.error has the
# same text). A missing file is nil too, without the log line: that is the
# ordinary state of a setting nobody has changed.
#
# Writing is line by line (update): only the lines of the keys given change,
# so a file somebody wrote by hand keeps its comments, its order and the keys
# nobody here knows about.
module FmrbToml
  # A value written as it is, unquoted (a colour name in /home/colors.toml).
  class Raw
    attr_reader :text

    def initialize(text)
      @text = text.to_s
    end
  end

  def self.raw(text)
    Raw.new(text)
  end

  # Why the last parse, load or update came back nil.
  def self.error
    @error
  end

  # Hash for the text, or nil when it is not TOML.
  def self.parse(text)
    @error = nil
    r = _parse(text.to_s)
    return r if r.is_a?(Hash)
    @error = r.to_s
    nil
  rescue => e
    @error = e.message
    nil
  end

  # Hash for the file; nil when there is no file or it is not TOML.
  def self.load(path)
    @error = nil
    unless File.exist?(path)
      @error = "#{path}: no such file"
      return nil
    end
    text = read(path)
    if text.nil?
      @error = "#{path}: cannot read"
      Log.warn("toml: #{@error}")
      return nil
    end
    h = parse(text)
    if h.nil?
      @error = "#{path}: #{@error}"
      Log.warn("toml: #{@error}")
    end
    h
  end

  # Sets keys in the file and leaves every other line as it is. values is a
  # Hash of name => value; a name is a top-level key ("rise_mg") or
  # "section.key" ("editor.bg"). A key that is not there is added (to its
  # section, which is added at the end when missing); nil removes the key.
  # The file is read back afterwards: the Hash it holds now is returned when
  # it has the values, nil otherwise (FmrbToml.error says why). A file that
  # is there but is not TOML is not touched -- it is somebody's text.
  def self.update(path, values)
    @error = nil
    text = File.exist?(path) ? read(path) : ""
    if text.nil?
      @error = "#{path}: cannot read"
      return nil
    end
    if parse(text).nil?
      @error = "#{path}: #{@error} (not written)"
      Log.warn("toml: #{@error}")
      return nil
    end
    lines = text.split("\n")
    values.each do |name, value|
      lines = set_line(lines, name.to_s, value)
    end
    begin
      File.open(path, "w") { |f| f.write(lines.join("\n") + "\n") }
    rescue => e
      @error = "#{path}: #{e.message}"
      return nil
    end
    h = load(path)
    return nil if h.nil?
    values.each do |name, value|
      got = dig(h, name.to_s)
      ok = if value.nil?
             got.nil?
           elsif value.is_a?(Raw)
             !got.nil?
           elsif value.is_a?(Symbol)
             got == value.to_s
           else
             got == value
           end
      unless ok
        @error = "#{path}: #{name} reads back #{got.inspect}"
        return nil
      end
    end
    h
  end

  # The TOML text of one value: a String quoted and escaped, numbers and
  # true/false as they are, an Array or a Hash inline.
  def self.dump_value(v)
    return v.text if v.is_a?(Raw)
    return v.to_s if v == true || v == false || v.is_a?(Integer)
    return dump_float(v) if v.is_a?(Float)
    return quote(v.to_s) if v.is_a?(String) || v.is_a?(Symbol)
    if v.is_a?(Array)
      out = []
      v.each { |e| out << dump_value(e) }
      return "[" + out.join(", ") + "]"
    end
    if v.is_a?(Hash)
      out = []
      v.each { |k, e| out << (dump_key(k.to_s) + " = " + dump_value(e)) }
      return "{ " + out.join(", ") + " }"
    end
    raise ArgumentError, "no TOML for #{v.class}"
  end

  HEX = "0123456789ABCDEF"

  def self.quote(s)
    out = "\"".dup
    i = 0
    n = s.bytesize
    while i < n
      b = s.getbyte(i)
      if b == 34
        out << "\\\""
      elsif b == 92
        out << "\\\\"
      elsif b == 10
        out << "\\n"
      elsif b == 9
        out << "\\t"
      elsif b == 13
        out << "\\r"
      elsif b < 32 || b == 127
        out << "\\u00" << HEX[b >> 4, 1] << HEX[b & 15, 1]
      else
        out << s.byteslice(i, 1)
      end
      i += 1
    end
    out << "\""
    out
  end

  def self.dump_float(f)
    return "nan" if f.nan?
    return (f > 0 ? "inf" : "-inf") if f.infinite?
    s = f.to_s
    s.include?(".") || s.include?("e") ? s : s + ".0"
  end

  # A bare key when it is one ([A-Za-z0-9_-]+), quoted otherwise.
  def self.dump_key(k)
    return quote(k) if k.empty?
    i = 0
    while i < k.bytesize
      b = k.getbyte(i)
      bare = (b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) || b == 95 || b == 45
      return quote(k) unless bare
      i += 1
    end
    k
  end

  # ---- inside ---------------------------------------------------------------

  def self.read(path)
    File.open(path, "r") { |f| f.read.to_s }
  rescue
    nil
  end

  # "a.b.c" -> ["a.b", "c"]; "k" -> ["", "k"]
  def self.split_name(name)
    dot = name.rindex(".")
    return ["", name] if dot.nil?
    [name[0, dot], name[dot + 1, name.length - dot - 1]]
  end

  def self.dig(h, name)
    parts = name.split(".")
    cur = h
    i = 0
    while i < parts.size
      return nil unless cur.is_a?(Hash)
      cur = cur[parts[i]]
      i += 1
    end
    cur
  end

  # "[editor]" -> "editor"; "[[mark]]" -> nil (an array of tables is never a
  # place update writes to).
  def self.header_name(t)
    return nil if t.start_with?("[[")
    close = t.index("]")
    return nil if close.nil?
    t[1, close - 1].strip
  end

  # The key of a "key = value" line (quotes taken off), or nil.
  def self.line_key(t)
    eq = t.index("=")
    return nil if eq.nil?
    k = t[0, eq].strip
    if k.length >= 2 && k.start_with?("\"") && k.end_with?("\"")
      k = k[1, k.length - 2]
    end
    k
  end

  def self.set_line(lines, name, value)
    sec, key = split_name(name)
    text = value.nil? ? nil : (dump_key(key) + " = " + dump_value(value))
    out = []
    cur = ""
    sec_seen = sec.empty?
    anchor = nil   # last line of the section, where a new key goes after
    placed = false
    i = 0
    while i < lines.size
      line = lines[i]
      t = line.strip
      i += 1
      if t.start_with?("[")
        cur = header_name(t)
        out << line
        if cur == sec
          sec_seen = true
          anchor = out.size - 1
        end
        next
      end
      if cur == sec && !t.empty? && !t.start_with?("#") && line_key(t) == key
        if !placed && text
          out << text
          placed = true
          anchor = out.size - 1
        end
        next                  # the old line goes, and any duplicate with it
      end
      out << line
      anchor = out.size - 1 if cur == sec && !t.empty?
    end
    return out if placed || text.nil?
    if sec_seen
      at = anchor.nil? ? 0 : anchor + 1
      out.insert(at, text)
    else
      out << "" if out.size > 0 && !out[out.size - 1].strip.empty?
      out << "[" + sec + "]"
      out << text
    end
    out
  end
end

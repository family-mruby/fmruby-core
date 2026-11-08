# FmrbReceiveConf: who may send files to this machine, kind by kind
# (/home/asterism/receive.toml; doc/asterism_file_transfer T2, T6).
#
# The file has two parts. Above the mark, what the machine's owner writes
# by hand (T2's tables, read as they always were):
#
#   [file]
#   from = ["pc-desk"]
#
# Below the mark, what the console sends (T6): the senders chosen for this
# machine in its member row (Admin > Members), written by the transfer app
# when the console calls policy_set, and only when the list changed (the
# flash is not written again for the same list):
#
#   # ---- managed by the console (below this line; edit above it) ----
#   [managed]
#   by = "console"                  # the node that sent it
#   at = "2026-11-09T03:12:00Z"     # when the list was changed there
#
#   [managed.file]
#   from = ["fmruby-aaaaaa", "fmruby-*"]
#
#   [managed.checkpoint]
#   from = []
#
# A kind is taken from the nodes of both lists (by hand and managed). The
# managed part stays as last written while the console cannot be reached
# (a peer-to-peer connection, the console stopped): no time limit. The two
# parts are read apart, so a line by hand that is not TOML does not lose
# the managed list (nor the other way round).
#
# A list holds node IDs and patterns: "*" any node, "fmruby-*" (a * is any
# run of letters).
module FmrbReceiveConf
  PATH = "/home/asterism/receive.toml"
  TMP_PATH = "/home/asterism/.receive.toml.new"
  MARK = "# ---- managed by the console"
  MARK_LINE = "# ---- managed by the console (below this line; edit above it) ----"
  KINDS = ["file", "checkpoint"]
  # what one list may hold (a call carries about 4 KB)
  ENTRY_MAX = 64
  LIST_MAX = 64
  AT_MAX = 40

  # [the text by hand, the managed text] ("" for a part not there), nil
  # when there is no file.
  def self.parts
    return nil unless ::File.exist?(PATH)
    text = ::File.open(PATH, "r") { |f| f.read }.to_s
    i = mark_at(text)
    return [text, ""] if i.nil?
    [text.byteslice(0, i).to_s, text.byteslice(i, text.bytesize - i).to_s]
  rescue => e
    Log.warn("transfer: #{PATH}: #{e.message}")
    nil
  end

  # Where the mark's line starts, nil without one.
  def self.mark_at(text)
    i = text.index(MARK)
    while i
      return i if i == 0 || text.byteslice(i - 1, 1) == "\n"
      i = text.index(MARK, i + 1)
    end
    nil
  end

  # The part by hand as a Hash (nil: none, or not TOML).
  def self.hand(p = parts)
    return nil if p.nil? || p[0].strip.empty?
    h = FmrbToml.parse(p[0])
    Log.warn("transfer: #{PATH} (by hand): #{FmrbToml.error}") if h.nil?
    h
  end

  # The managed part: {"by", "at", "file" => [...], "checkpoint" => [...]}
  # (a kind's list nil when not there), nil when there is none.
  def self.managed(p = parts)
    return nil if p.nil? || p[1].empty?
    h = FmrbToml.parse(p[1])
    if h.nil?
      Log.warn("transfer: #{PATH} (managed): #{FmrbToml.error}")
      return nil
    end
    m = h["managed"]
    return nil unless m.is_a?(Hash)
    out = { "by" => m["by"].to_s, "at" => m["at"].to_s }
    i = 0
    while i < KINDS.size
      t = m[KINDS[i]]
      out[KINDS[i]] = t.is_a?(Hash) ? strings(t["from"]) : nil
      i += 1
    end
    out
  end

  # The senders of a kind by hand (an Array), nil when the part by hand has
  # no table for it. The top-level from (the older form) is the
  # checkpoint's.
  def self.hand_senders(kind, h)
    return nil unless h.is_a?(Hash)
    t = h[kind]
    out = nil
    out = strings(t["from"]) if t.is_a?(Hash)
    if kind == "checkpoint" && h["from"].is_a?(Array)
      out = (out || []) + strings(h["from"])
    end
    out
  end

  # {"hand" => [...] | nil, "managed" => [...] | nil, "by", "at"} for a kind.
  def self.senders(kind)
    p = parts
    m = managed(p)
    { "hand" => hand_senders(kind, hand(p)), "managed" => m ? m[kind] : nil,
      "by" => m ? m["by"] : "", "at" => m ? m["at"] : "" }
  end

  # Whether a list lets node in.
  def self.allows?(list, node)
    return false unless list.is_a?(Array)
    i = 0
    while i < list.size
      return true if match?(list[i].to_s, node.to_s)
      i += 1
    end
    false
  end

  # A pattern with * (any run of letters, "" too) against s.
  def self.match?(pat, s)
    return true if pat == "*"
    return pat == s unless pat.index("*")
    pieces = pat.split("*", -1)
    first = pieces[0]
    last = pieces[pieces.size - 1]
    return false unless s.start_with?(first) && s.end_with?(last)
    return false if first.bytesize + last.bytesize > s.bytesize
    pos = first.bytesize
    stop = s.bytesize - last.bytesize
    i = 1
    while i < pieces.size - 1
      pc = pieces[i]
      unless pc.empty?
        at = s.index(pc, pos)
        return false if at.nil? || at + pc.bytesize > stop
        pos = at + pc.bytesize
      end
      i += 1
    end
    true
  end

  # A list as written in the file: node IDs and patterns (letters, digits,
  # . _ - and *). Raises ArgumentError for anything else.
  def self.checked_list(v, kind)
    raise ArgumentError, "#{kind}: not a list" unless v.nil? || v.is_a?(Array)
    out = []
    return out if v.nil?
    raise ArgumentError, "#{kind}: more than #{LIST_MAX} senders" if v.size > LIST_MAX
    i = 0
    while i < v.size
      e = v[i]
      unless e.is_a?(String) && !e.empty? && e.bytesize <= ENTRY_MAX && plain?(e)
        raise ArgumentError, "#{kind}: not a node ID or pattern: #{e.to_s.byteslice(0, ENTRY_MAX)}"
      end
      out << e unless out.include?(e)
      i += 1
    end
    out
  end

  def self.plain?(s)
    i = 0
    while i < s.bytesize
      c = s.getbyte(i)
      ok = (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) ||
           c == 45 || c == 46 || c == 95 || c == 42
      return false unless ok
      i += 1
    end
    true
  end

  def self.at_ok?(s)
    return false unless s.is_a?(String) && s.bytesize <= AT_MAX
    i = 0
    while i < s.bytesize
      c = s.getbyte(i)
      ok = (c >= 48 && c <= 58) || c == 84 || c == 90 || c == 43 || c == 45 || c == 46
      return false unless ok
      i += 1
    end
    true
  end

  # The managed part's text for these lists.
  def self.block(by, at, lists)
    s = "#{MARK_LINE}\n[managed]\nby = \"#{by}\"\nat = \"#{at}\"\n"
    i = 0
    while i < KINDS.size
      k = KINDS[i]
      s << "\n[managed.#{k}]\nfrom = [#{quoted(lists[k])}]\n"
      i += 1
    end
    s
  end

  def self.quoted(list)
    out = []
    i = 0
    while i < list.size
      out << "\"#{list[i]}\""
      i += 1
    end
    out.join(", ")
  end

  # Writes the managed part (the part by hand stays as it is), only when its
  # text is not the one there. lists: {"file" => [...], "checkpoint" => [...]}
  # (checked_list). Returns whether the file was written.
  def self.write_managed(by, at, lists)
    p = parts || ["", ""]
    text = block(by, at, lists)
    return false if p[1] == text
    head = p[0]
    head = "#{head}\n" unless head.empty? || head.end_with?("\n")
    head = "#{head}\n" unless head.empty? || head.end_with?("\n\n")
    ::File.open(TMP_PATH, "w") { |f| f.write(head + text) }
    ::File.delete(PATH) if ::File.exist?(PATH)
    ::File.rename(TMP_PATH, PATH)
    true
  end

  # The lines asend -l shows: who may send each kind, and where that
  # comes from.
  def self.lines
    return ["no #{PATH}: nobody may send files or apps here"] unless ::File.exist?(PATH)
    p = parts
    h = hand(p)
    m = managed(p)
    out = []
    i = 0
    while i < KINDS.size
      k = KINDS[i]
      by_hand = hand_senders(k, h)
      by_console = m ? m[k] : nil
      if by_hand.nil? && by_console.nil?
        out << "#{k}: nobody (no [#{k}])"
      else
        out << "#{k}:"
        list_lines(out, by_hand, "by hand")
        list_lines(out, by_console, "from #{m['by']}") if m
      end
      i += 1
    end
    out << (m ? "managed by #{m['by']}, changed there at #{m['at']}" : "nothing from the console yet")
    out
  end

  def self.list_lines(out, list, where)
    return if list.nil?
    if list.empty?
      out << "  (none)  #{where}"
      return
    end
    i = 0
    while i < list.size
      out << "  #{list[i]}  #{where}"
      i += 1
    end
  end

  def self.strings(v)
    out = []
    return out unless v.is_a?(Array)
    i = 0
    while i < v.size
      out << v[i].to_s
      i += 1
    end
    out
  end
end

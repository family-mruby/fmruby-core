# FmrbReceiveConf: whether this machine receives files, and from whom, kind
# by kind (receive.toml; doc/asterism_file_transfer T2, T6, T7).
#
# Where it is read (T7, the same shape as the WiFi settings): the user's
# /home/asterism/receive.toml when there is one, else the one the system
# ships, /etc/asterism/receive.toml (receiving off, nobody listed). This
# module is the one place the apps and the shell read it from (the transfer
# app, the shell's areceive and asend -l, the Receiving app); the kernel,
# a Spinel program, reads the enabled line by itself
# (fmrb_kernel.rb receiving_enabled?) with the same rules.
#
# The file has two parts. Above the mark, what the machine's owner writes
# by hand (T2's tables, read as they always were) and the switch:
#
#   enabled = true                  # receive at all (T7; no line: off)
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
# Nothing is taken unless enabled = true is in the part by hand: a file
# without the line is off (T7; the console's list never turns it on). When
# it is on, a kind is taken from the nodes of both lists (by hand and
# managed). The managed part stays as last written while the console cannot
# be reached (a peer-to-peer connection, the console stopped): no time
# limit. The two parts are read apart, so a line by hand that is not TOML
# does not lose the managed list (nor the other way round).
#
# A list holds node IDs and patterns: "*" any node, "fmruby-*" (a * is any
# run of letters).
#
# Changing it (T7: the Receiving app and the shell's areceive):
# set_enabled, allow and deny change the part by hand only, line by line
# (comments, order and keys nobody here knows stay), never the managed part
# or the mark. The result is written to /home/asterism/receive.toml, made
# from the copy in /etc the first time, and only when its text changed.
# tell_transfer then starts the transfer app (on) or asks it to end once
# what is coming in is in (off).
module FmrbReceiveConf
  PATH = "/home/asterism/receive.toml"
  SYS_PATH = "/etc/asterism/receive.toml"
  DIR = "/home/asterism"
  TMP_PATH = "/home/asterism/.receive.toml.new"
  MARK = "# ---- managed by the console"
  MARK_LINE = "# ---- managed by the console (below this line; edit above it) ----"
  KINDS = ["file", "checkpoint"]
  ANYONE = "*"
  # what one list may hold (a call carries about 4 KB)
  ENTRY_MAX = 64
  LIST_MAX = 64
  AT_MAX = 40
  # The transfer app (started when receiving is turned on, told when it is
  # turned off: it ends once what is coming in is in).
  TRANSFER_APP = "/usr/share/transfer/transfer.app.rb"
  TRANSFER_NAME = "Transfer"
  CTL_TOPIC = "transfer/ctl"

  # The file read: the user's, else the system's (nil: neither).
  def self.path
    return PATH if ::File.exist?(PATH)
    return SYS_PATH if ::File.exist?(SYS_PATH)
    nil
  end

  # [the text by hand, the managed text] ("" for a part not there), nil
  # when there is no file.
  def self.parts
    f = path
    return nil if f.nil?
    text = ::File.open(f, "r") { |io| io.read }.to_s
    i = mark_at(text)
    return [text, ""] if i.nil?
    [text.byteslice(0, i).to_s, text.byteslice(i, text.bytesize - i).to_s]
  rescue => e
    Log.warn("transfer: #{f}: #{e.message}")
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
    Log.warn("transfer: #{path} (by hand): #{FmrbToml.error}") if h.nil?
    h
  end

  # Receiving is on: enabled = true in the part by hand (no line, another
  # value, a part that is not TOML: off).
  def self.enabled?(p = parts)
    h = hand(p)
    h.is_a?(Hash) && h["enabled"] == true
  end

  # The managed part: {"by", "at", "file" => [...], "checkpoint" => [...]}
  # (a kind's list nil when not there), nil when there is none.
  def self.managed(p = parts)
    return nil if p.nil? || p[1].empty?
    h = FmrbToml.parse(p[1])
    if h.nil?
      Log.warn("transfer: #{path} (managed): #{FmrbToml.error}")
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

  # {"enabled", "hand" => [...] | nil, "managed" => [...] | nil, "by", "at"}
  # for a kind (one read of the file).
  def self.senders(kind)
    p = parts
    h = hand(p)
    m = managed(p)
    { "enabled" => h.is_a?(Hash) && h["enabled"] == true,
      "hand" => hand_senders(kind, h), "managed" => m ? m[kind] : nil,
      "by" => m ? m["by"] : "", "at" => m ? m["at"] : "" }
  end

  # Everything the Receiving app shows (one read): {"enabled", "path" (nil:
  # no file), "hand_ok" (the part by hand is TOML, or empty), "by", "at",
  # and per kind {"hand" => [...] | nil, "managed" => [...] | nil}}.
  def self.view
    p = parts
    h = hand(p)
    m = managed(p)
    out = { "enabled" => h.is_a?(Hash) && h["enabled"] == true, "path" => path,
            "hand_ok" => p.nil? || p[0].strip.empty? || h.is_a?(Hash),
            "by" => m ? m["by"] : "", "at" => m ? m["at"] : "" }
    i = 0
    while i < KINDS.size
      k = KINDS[i]
      out[k] = { "hand" => hand_senders(k, h), "managed" => m ? m[k] : nil }
      i += 1
    end
    out
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
      unless node_ok?(e)
        raise ArgumentError, "#{kind}: not a node ID or pattern: #{e.to_s.byteslice(0, ENTRY_MAX)}"
      end
      out << e unless out.include?(e)
      i += 1
    end
    out
  end

  # One entry of a list: a node ID or pattern of letters, digits, . _ - *,
  # up to ENTRY_MAX bytes.
  def self.node_ok?(e)
    e.is_a?(String) && !e.empty? && e.bytesize <= ENTRY_MAX && plain?(e)
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

  # Writes the managed part (the part by hand stays as it is; the system's
  # copy is the part by hand the first time), only when its text is not the
  # one there. lists: {"file" => [...], "checkpoint" => [...]}
  # (checked_list). Returns whether the file was written.
  def self.write_managed(by, at, lists)
    p = parts || ["", ""]
    text = block(by, at, lists)
    return false if p[1] == text && ::File.exist?(PATH)
    write_file(joined(p[0], text))
    true
  end

  # ---- changing the part by hand (T7) ----

  # Turns receiving on or off. Returns whether the file was written (false:
  # it was so already). Raises ArgumentError when the part by hand is not
  # TOML (somebody's text is not rewritten).
  def self.set_enabled(on)
    p = parts || ["", ""]
    h = hand_for_edit(p)
    return false if (h["enabled"] == true) == on
    head = set_key(p[0], "", "enabled", on ? "true" : "false")
    save_hand(p, head, "enabled", on)
  end

  # Lets node send kind (a node ID or a pattern, "*" anyone). Returns
  # whether the file was written (false: it was in the list by hand).
  def self.allow(kind, node)
    k = kind_of(kind)
    n = node.to_s
    raise ArgumentError, "not a node ID or pattern: #{n.byteslice(0, ENTRY_MAX)}" unless node_ok?(n)
    p = parts || ["", ""]
    h = hand_for_edit(p)
    list = table_list(h, k)
    return false if list.include?(n) || (k == "checkpoint" && strings(h["from"]).include?(n))
    raise ArgumentError, "#{k}: more than #{LIST_MAX} senders" if list.size >= LIST_MAX
    list << n
    head = set_key(p[0], k, "from", "[#{quoted(list)}]")
    save_hand(p, head, k, list)
  end

  # Takes node out of kind's list by hand (also out of the older top-level
  # from, for checkpoint). Returns whether the file was written. Raises
  # ArgumentError when node is not in it (in the console's list only: that
  # is changed on the console).
  def self.deny(kind, node)
    k = kind_of(kind)
    n = node.to_s
    p = parts || ["", ""]
    h = hand_for_edit(p)
    list = table_list(h, k)
    top = k == "checkpoint" ? strings(h["from"]) : []
    unless list.include?(n) || top.include?(n)
      m = managed(p)
      if m && m[k].is_a?(Array) && m[k].include?(n)
        raise ArgumentError, "#{n} is in the list from #{m['by']} (#{k}); it is changed there"
      end
      raise ArgumentError, "#{n} is not in the #{k} list"
    end
    head = p[0]
    if list.include?(n)
      list = without(list, n)
      head = set_key(head, k, "from", "[#{quoted(list)}]")
    end
    if top.include?(n)
      head = set_key(head, "", "from", "[#{quoted(without(top, n))}]")
    end
    save_hand(p, head, k, nil)
  end

  # Starts the transfer app when receiving is on (and it is not running),
  # and tells a running one to look at receive.toml again: off, it ends
  # once what is coming in is in. app: the FmrbApp asking.
  def self.tell_transfer(app, on)
    running = transfer_running?
    if on && !running
      app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                       { "cmd" => "spawn", "app_name" => TRANSFER_APP })
      return :started
    end
    return :none unless running
    app.publish(CTL_TOPIC, { "cmd" => "receive_conf" })
    on ? :running : :stopping
  end

  def self.transfer_running?
    procs = FmrbApp.ps
    return false unless procs
    i = 0
    while i < procs.size
      pr = procs[i]
      return true if pr[:name] == TRANSFER_NAME && pr[:state] != FmrbConst::PROC_STATE_FREE
      i += 1
    end
    false
  end

  # ---- what asend -l and areceive show ----

  # The lines: on or off and the file read, who may send each kind, and
  # where that comes from. A checkpoint list with "*" (anyone may start apps
  # here) is marked with "!".
  def self.lines
    f = path
    return ["receiving: off (no #{SYS_PATH} nor #{PATH}): nobody may send files or apps here"] if f.nil?
    p = parts
    h = hand(p)
    m = managed(p)
    out = []
    on = h.is_a?(Hash) && h["enabled"] == true
    out << (on ? "receiving: on (#{f})" : "receiving: off (#{f}): nothing is taken; areceive on turns it on")
    out << "#{f} (by hand) is not TOML: #{FmrbToml.error}" if !p.nil? && !p[0].strip.empty? && h.nil?
    i = 0
    while i < KINDS.size
      k = KINDS[i]
      by_hand = hand_senders(k, h)
      by_console = m ? m[k] : nil
      if by_hand.nil? && by_console.nil?
        out << "#{k}: nobody (no [#{k}])"
      else
        wide = k == "checkpoint" && (allows_anyone?(by_hand) || allows_anyone?(by_console))
        out << "#{wide ? '! ' : ''}#{k}:"
        list_lines(out, by_hand, "by hand", k)
        list_lines(out, by_console, "from #{m['by']}", k) if m
        out << "! anyone on the network may run apps on this machine" if wide
      end
      i += 1
    end
    out << (m ? "managed by #{m['by']}, changed there at #{m['at']}" : "nothing from the console yet")
    out
  end

  def self.list_lines(out, list, where, kind = "")
    return if list.nil?
    if list.empty?
      out << "  (none)  #{where}"
      return
    end
    i = 0
    while i < list.size
      e = list[i]
      mark = kind == "checkpoint" && e == ANYONE ? "! " : "  "
      out << "#{mark}#{e}  #{where}#{e == ANYONE ? ' (anyone)' : ''}"
      i += 1
    end
  end

  def self.without(list, n)
    out = []
    i = 0
    while i < list.size
      out << list[i] unless list[i] == n
      i += 1
    end
    out
  end

  def self.allows_anyone?(list)
    list.is_a?(Array) && list.include?(ANYONE)
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

  # ---- inside ----

  def self.kind_of(kind)
    k = kind.to_s
    raise ArgumentError, "not a kind: #{k} (file or checkpoint)" unless KINDS.include?(k)
    k
  end

  # The part by hand as a Hash to change ({} when empty). Raises when it is
  # not TOML.
  def self.hand_for_edit(p)
    return {} if p[0].strip.empty?
    h = FmrbToml.parse(p[0])
    raise ArgumentError, "#{path} (by hand) is not TOML (#{FmrbToml.error}); not changed" if h.nil?
    h
  end

  # The [kind] from by hand (a new Array).
  def self.table_list(h, k)
    t = h[k]
    t.is_a?(Hash) ? strings(t["from"]) : []
  end

  # The new part by hand, read back (what was asked for must be what the
  # file now says), then written with the managed part as it is. what:
  # "enabled" (want: true/false) or a kind (want: its list; nil: only
  # checked to be TOML).
  def self.save_hand(p, head, what, want)
    h = FmrbToml.parse(head)
    raise ArgumentError, "the change would not be TOML (#{FmrbToml.error}); not written" if h.nil?
    got = what == "enabled" ? h["enabled"] == true : (want.nil? ? nil : table_list(h, what))
    if !want.nil? && got != want
      raise ArgumentError, "the change does not read back (#{what}); not written"
    end
    text = joined(head, p[1])
    return false if ::File.exist?(PATH) && text == p[0] + p[1]
    write_file(text)
    true
  end

  # The part by hand and the managed part as one text (a blank line between
  # them).
  def self.joined(head, tail)
    return head if tail.empty?
    h = head
    h = "#{h}\n" unless h.empty? || h.end_with?("\n")
    h = "#{h}\n" unless h.empty? || h.end_with?("\n\n")
    h + tail
  end

  def self.write_file(text)
    Dir.mkdir(DIR) unless Dir.exist?(DIR)
    ::File.open(TMP_PATH, "w") { |f| f.write(text) }
    ::File.delete(PATH) if ::File.exist?(PATH)
    ::File.rename(TMP_PATH, PATH)
    nil
  end

  # text with key of table sec ("" the top) set to value (its TOML text):
  # the line (with the lines of a value that goes on over several) replaced,
  # or a line added -- at the end of the table's keys, a table added at the
  # end when it is not there; a top-level key after the top-level keys, or
  # before the first table (after the comments that open the file).
  def self.set_key(text, sec, key, value)
    lines = text.split("\n", -1)
    lines.pop if !lines.empty? && lines[lines.size - 1].empty?
    line = "#{key} = #{value}"
    cur = ""
    seen = sec.empty?
    last = -1           # the last line of a key of sec
    first_table = -1
    i = 0
    while i < lines.size
      t = lines[i].strip
      if t.start_with?("[")
        first_table = i if first_table < 0
        cur = table_name(t)
        if cur == sec
          seen = true
          last = i
        end
        i += 1
        next
      end
      if t.empty? || t.start_with?("#")
        i += 1
        next
      end
      stop = value_end(lines, i)
      if cur == sec && key_of(t) == key
        lines[i] = line
        k = stop
        while k > i
          lines.delete_at(k)
          k -= 1
        end
        return lines.join("\n") + "\n"
      end
      last = stop if cur == sec
      i = stop + 1
    end
    if !seen
      lines << "" unless lines.empty? || lines[lines.size - 1].strip.empty?
      lines << "[#{sec}]"
      lines << line
    elsif last >= 0
      lines.insert(last + 1, line)
    else
      # a top-level key in a file with none: after the comments that open
      # it, before the first table
      at = 0
      while at < lines.size && lines[at].strip.start_with?("#")
        at += 1
      end
      at = first_table if first_table >= 0 && first_table < at
      lines.insert(at, line)
      lines.insert(at + 1, "") if at + 1 < lines.size && lines[at + 1].strip.start_with?("[")
    end
    lines.join("\n") + "\n"
  end

  # "[file]" -> "file" ("[[x]]" -> "[x]", never a table set_key writes to).
  def self.table_name(t)
    close = t.index("]")
    return "" if close.nil?
    t[1, close - 1].to_s.strip
  end

  # The key of a "key = value" line (quotes taken off), nil without "=".
  def self.key_of(t)
    eq = t.index("=")
    return nil if eq.nil?
    k = t[0, eq].to_s.strip
    k = k[1, k.length - 2].to_s if k.length >= 2 && k.start_with?("\"") && k.end_with?("\"")
    k
  end

  # The last line of the value that starts on line i: an array goes on
  # until its brackets close (strings and comments skipped).
  def self.value_end(lines, i)
    depth = 0
    k = i
    first = true
    while k < lines.size
      s = lines[k]
      j = 0
      if first
        eq = s.index("=")
        j = eq.nil? ? s.bytesize : eq + 1
        first = false
      end
      q = 0               # the quote a string is in (34 ", 39 '), 0 none
      while j < s.bytesize
        c = s.getbyte(j)
        if q != 0
          if c == 92 && q == 34
            j += 1
          elsif c == q
            q = 0
          end
        elsif c == 34 || c == 39
          q = c
        elsif c == 35
          break
        elsif c == 91 || c == 123
          depth += 1
        elsif c == 93 || c == 125
          depth -= 1
        end
        j += 1
      end
      return k if depth <= 0
      k += 1
    end
    lines.size - 1
  end
end

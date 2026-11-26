# The remote shell's machine side (doc/remote_shell R1): the commands that
# look at the machine, run without a screen in the transfer app, which
# reads this file only while rshell_enabled = true in receive.toml (a
# machine with it off holds none of it) and stays up for it as for its
# other switches (TransferApp#rshell). Door#switch exposes the door as
# <node>/transfer/rshell while it is on.
#
# A line at a time: the console's Terminal sends one line, the machine runs
# it and answers with the text. No character-by-character exchange, no full
# screen (less, the editor, irb), no Ruby of the caller's own.
#
# The calls (caller: :from on every one; the calling node's ID):
#
#   open()                  -> {"id", "cwd", "version" => {"os", "board",
#                              "rshell"}, "commands" => {name => "read" |
#                              "write"}, "out_max", "total_max", "idle_ms"}
#   exec_read(id, line)     a command that only looks
#   exec_write(id, line)    any command (the ones that change the machine
#                           are not in this version: they answer so)
#                           -> {"out", "code", "cwd", "more", "cut"}
#   more(id, pos)           the output from pos on (pos: the "more" of the
#                           last answer; the same pos may be asked again)
#                           -> {"out", "more", "cut"}
#   complete(id, line, pos) -> {"word", "items", "cut"}: what may stand for
#                           the word under pos (pos and word count
#                           characters; pos nil or past the end: the end)
#   read(id, path, pos, len)-> {"data", "pos", "size"}: up to READ_MAX bytes
#                           of a file from pos (get, doc/remote_shell R4)
#   close(id)               -> true
#
# Refusals (the switch off, a node not let in, another session, no such
# session) are raised: the caller sees an error with the reason. A command
# that fails answers with code 1 (2: a usage error, 126: a command that
# changes the machine sent to exec_read, 127: no such command).
#
# The output of one call is at most OUT_MAX bytes, cut where a UTF-8
# character starts; the rest is had with more. A command's whole output is
# at most TOTAL_MAX bytes: past that it is cut ("cut" => true, and a note).
# A file shown by cat is read piece by piece from the file (never all at
# once), the one File kept open between calls while the reads follow on.
#
# The secrets (FmrbFile.secret_path?, fmrb_secret_path.c: the WiFi password,
# the API keys and /home/asterism with everything in it) are never read: a
# path is folded (".", "..", "//") before it is looked at, and ls and the
# completion name the folder but never show what is in it.
module RShell
  PROTO = 1
  OUT_MAX = 8192
  TOTAL_MAX = 65536
  READ_MAX = 8192
  COMPLETE_MAX = 200
  LINE_MAX = 1024
  IDLE_MS = 300000
  HOME = "/home"
  # bytes read at a time while skipping to a position or looking for the
  # last lines
  CHUNK = 4096
  LINES_DEFAULT = 10
  # where df looks (the ones that are there)
  MOUNTS = ["/home", "/tmp", "/sd", "/mnt/sd"]
  READ = "read"
  WRITE = "write"
  # name => [kind, usage, what it does]; the order is help's
  TABLE = [
    ["pwd", READ, "pwd", "where this session is"],
    ["cd", READ, "cd [dir]", "go to dir (/home when left out)"],
    ["ls", READ, "ls [-l] [path...]", "the names in a directory (-l: sizes)"],
    ["cat", READ, "cat <file>", "a file's text"],
    ["head", READ, "head [-n N] <file>", "the first N lines (10)"],
    ["tail", READ, "tail [-n N] <file>", "the last N lines (10)"],
    ["stat", READ, "stat <path>", "a file's or a directory's type and size"],
    ["df", READ, "df", "the free space of the storage"],
    ["free", READ, "free", "the memory"],
    ["ps", READ, "ps", "the tasks"],
    ["ver", READ, "ver", "the versions of this machine"],
    ["help", READ, "help", "this list"],
    ["cp", WRITE, "cp <src> <dst>", "copy a file"],
    ["mv", WRITE, "mv <src> <dst>", "move or rename"],
    ["rm", WRITE, "rm <path...>", "delete files or empty directories"],
    ["mkdir", WRITE, "mkdir <dir>", "make a directory"],
    ["kill", WRITE, "kill <pid>", "end an app"],
    ["open", WRITE, "open <file>", "start an app, or open a file with its app"]
  ]
  OBJECT = "rshell"
  DOC = "the remote shell: commands a line at a time (doc/remote_shell)"
  # What the transfer app exposes (with the arguments' names for the
  # readers that show them: asterism object info, the console)
  METHODS = {
    "open" => { args: [], doc: "a session: its id, where it is, the version and the commands (read / write)" },
    "exec_read" => { args: ["id", "line"], doc: "a command that only looks -> out, code, cwd, more, cut" },
    "exec_write" => { args: ["id", "line"], doc: "any command (none that changes the machine yet)" },
    "more" => { args: ["id", "pos"], doc: "the output from pos on (the more of the last answer)" },
    "complete" => { args: ["id", "line", "pos"], doc: "what may stand for the word under pos -> word, items, cut" },
    "read" => { args: ["id", "path", "pos", "len"], doc: "up to 8 KB of a file from pos -> data, pos, size" },
    "close" => { args: ["id"], doc: "ends the session" }
  }
  # FmrbApp.ps's numbers, as the shell names them
  STATE_NAMES = ["free", "init", "running", "suspended", "stopping"]
  TYPE_NAMES = ["kernel", "system", "user"]
  ENGINE_NAMES = ["mruby", "lua", "basic", "native", "python", "wasm"]

  # {name => "read" | "write"} (open's answer)
  def self.commands
    h = {}
    i = 0
    while i < TABLE.size
      h[TABLE[i][0]] = TABLE[i][1]
      i += 1
    end
    h
  end

  # The TABLE row of a command, nil when there is none.
  def self.row(name)
    i = 0
    while i < TABLE.size
      return TABLE[i] if TABLE[i][0] == name
      i += 1
    end
    nil
  end

  # ---- reading a line ----

  # The words of a line: [[word, start byte], ...] and whether a quote is
  # left open. Blanks (space, tab) part words; "a b" and a\ b keep one.
  def self.split_line(line)
    out = []
    n = line.bytesize
    i = 0
    open = false
    while i < n
      c = line.getbyte(i)
      if c == 32 || c == 9
        i += 1
        next
      end
      start = i
      w = ""
      quote = false
      while i < n
        c = line.getbyte(i)
        if c == 92 && i + 1 < n
          w << line.byteslice(i + 1, 1)
          i += 2
          next
        end
        if c == 34
          quote = !quote
          i += 1
          next
        end
        break if !quote && (c == 32 || c == 9)
        w << line.byteslice(i, 1)
        i += 1
      end
      open = quote
      out << [w, start]
    end
    [out, open]
  end

  # The words only.
  def self.words(line)
    a = split_line(line)[0]
    out = []
    i = 0
    while i < a.size
      out << a[i][0]
      i += 1
    end
    out
  end

  # A name as it is typed: a blank, a quote and a backslash get a backslash.
  def self.escape(s)
    out = ""
    i = 0
    n = s.bytesize
    while i < n
      c = s.getbyte(i)
      out << "\\" if c == 32 || c == 9 || c == 34 || c == 92
      out << s.byteslice(i, 1)
      i += 1
    end
    out
  end

  # ---- paths ----

  # path from cwd, absolute, with ".", ".." and "//" folded ("/" at most at
  # the top: ".." past it stays there).
  def self.resolve(cwd, path)
    p = path.to_s
    p = cwd.to_s if p.empty?
    p = "#{cwd}/#{p}" unless p.start_with?("/")
    parts = p.split("/")
    kept = []
    i = 0
    while i < parts.size
      s = parts[i]
      i += 1
      next if s.empty? || s == "."
      if s == ".."
        kept.pop
        next
      end
      kept << s
    end
    return "/" if kept.empty?
    "/" + kept.join("/")
  end

  # path's directory and its last name
  def self.join(dir, name)
    dir == "/" ? "/#{name}" : "#{dir}/#{name}"
  end

  def self.secret?(path)
    FmrbFile.secret_path?(path) == true
  end

  # ---- cutting the output ----

  # The bytes of s to send when at most n may go: n, or less so that the
  # cut falls where a UTF-8 character starts (a broken sequence is cut at
  # n as it is).
  def self.utf8_cut(s, n)
    return s.bytesize if s.bytesize <= n
    k = n
    back = 0
    while k > 0 && back < 4
      c = s.getbyte(k)
      return k if c < 0x80 || c >= 0xC0
      k -= 1
      back += 1
    end
    n
  end

  # How many bytes of s hold whole characters: s.bytesize, or less when
  # it ends in the first bytes of one (a broken sequence counts as whole).
  def self.utf8_whole(s)
    n = s.bytesize
    i = n - 1
    back = 0
    while i >= 0 && back < 4
      c = s.getbyte(i)
      return n if c < 0x80
      if c >= 0xC0
        need = c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2)
        return i + need <= n ? n : i
      end
      i -= 1
      back += 1
    end
    n
  end

  # n bytes as KB (MB from 10 MB on)
  def self.kb(n)
    n = n.to_i
    return "#{(n + 524288) / 1048576} MB" if n >= 10485760
    "#{(n + 512) / 1024} KB"
  end

  def self.pad(s, n)
    s = s.to_s
    while s.bytesize < n
      s = " " + s
    end
    s
  end

  def self.now_ms
    Machine.board_millis
  end

  # One session: where it is, and the output being handed out. now: the
  # clock (milliseconds) when it was opened.
  class Session
    attr_reader :id, :from, :cwd
    attr_accessor :at

    def initialize(id, from, now)
      @id = id
      @from = from
      @at = now
      @cwd = HOME
      @text = nil       # a command's output, at most TOTAL_MAX bytes
      @cut = false      # ... and whether it was cut there
      @src = nil        # cat: the file whose text is the output
      @src_size = 0
      @rd = nil         # the File kept open while the reads follow on
      @rd_path = nil
      @rd_pos = 0
    end

    # ---- the calls ----

    # A line. write: whether the commands that change the machine are
    # taken (exec_write).
    def exec(line, write)
      @text = nil
      @src = nil
      @cut = false
      l = line.to_s
      return answer(error("the line is longer than #{LINE_MAX} bytes"), 2) if l.bytesize > LINE_MAX
      w = RShell.words(l)
      return answer("", 0) if w.empty?
      name = w[0]
      args = w[1, w.size - 1] || []
      r = RShell.row(name)
      return answer("#{name}: no such command (help lists them)\n", 127) if r.nil?
      if r[1] == WRITE
        unless write
          return answer("#{name}: changes the machine; not taken by exec_read (exec_write)\n", 126)
        end
        return answer("#{name}: not in this version of the remote shell\n", 127)
      end
      run(name, args)
    rescue => e
      answer("#{name}: #{e.message}\n", 1)
    end

    # The output from pos on.
    def more(pos)
      p = pos.to_i
      raise ArgumentError, "pos: #{pos.inspect} is not a position" if p < 0
      if @src
        page = file_page(p)
      elsif @text
        page = text_page(p)
      else
        page = ["", nil]
      end
      { "out" => page[0], "more" => page[1], "cut" => @cut && page[1].nil? }
    end

    # What may stand for the word under pos.
    def complete(line, pos)
      l = line.to_s
      l = l.byteslice(0, LINE_MAX).to_s if l.bytesize > LINE_MAX
      len = l.length
      c = pos.nil? || pos.to_i < 0 || pos.to_i > len ? len : pos.to_i
      head = l[0, c].to_s
      sp = RShell.split_line(head)
      a = sp[0]
      last = a.empty? ? nil : a[a.size - 1]
      blank_end = !sp[1] && head.bytesize > 0 && (head.getbyte(head.bytesize - 1) == 32 ||
                                                  head.getbyte(head.bytesize - 1) == 9)
      if last.nil? || (blank_end && !escaped_end?(head))
        word = ""
        start = head.bytesize
        first = a.empty?
      else
        word = last[0]
        start = last[1]
        first = a.size == 1
      end
      items = first ? command_items(word) : path_items(word)
      cut = items.size > COMPLETE_MAX
      items = items[0, COMPLETE_MAX] if cut
      { "word" => head.byteslice(0, start).to_s.length, "items" => items, "cut" => cut }
    end

    # Up to len bytes of a file from pos.
    def read(path, pos, len)
      p = RShell.resolve(@cwd, path)
      raise ArgumentError, "#{p}: kept from the remote shell (keys and passwords)" if RShell.secret?(p)
      raise ArgumentError, "#{p}: not a file" unless File.file?(p)
      at = pos.to_i
      n = len.to_i
      raise ArgumentError, "pos: #{pos.inspect} is not a position" if at < 0
      raise ArgumentError, "len: 1 to #{READ_MAX}" if n < 1 || n > READ_MAX
      size = FmrbFile.size(p).to_i
      data = at >= size ? "" : read_at(p, at, n)
      { "data" => data, "pos" => at, "size" => size }
    end

    def close
      close_reader
      @text = nil
      @src = nil
      nil
    end

    # ---- the commands ----

    def run(name, args)
      case name
      when "pwd" then answer("#{@cwd}\n", 0)
      when "cd" then cmd_cd(args)
      when "ls" then cmd_ls(args)
      when "cat" then cmd_cat(args)
      when "head" then cmd_head(args, false)
      when "tail" then cmd_head(args, true)
      when "stat" then cmd_stat(args)
      when "df" then cmd_df
      when "free" then cmd_free
      when "ps" then cmd_ps
      when "ver" then cmd_ver
      when "help" then cmd_help
      else answer("#{name}: no such command\n", 127)
      end
    end

    def cmd_cd(args)
      return answer("usage: cd [dir]\n", 2) if args.size > 1
      p = RShell.resolve(@cwd, args.empty? ? HOME : args[0])
      return answer(kept("cd", p), 1) if RShell.secret?(p)
      return answer("cd: #{p}: no such directory\n", 1) unless File.directory?(p)
      @cwd = p
      answer("", 0)
    end

    def cmd_ls(args)
      long = false
      paths = []
      i = 0
      while i < args.size
        a = args[i]
        if a == "-l"
          long = true
        elsif a.start_with?("-") && a.bytesize > 1
          return answer("usage: ls [-l] [path...]\n", 2)
        else
          paths << a
        end
        i += 1
      end
      paths << "." if paths.empty?
      out = ""
      code = 0
      i = 0
      while i < paths.size
        p = RShell.resolve(@cwd, paths[i])
        out << "\n" if paths.size > 1 && i > 0
        out << "#{p}:\n" if paths.size > 1
        if RShell.secret?(p) && (File.directory?(p) || RShell.secret?(RShell.resolve(p, "..")))
          # the key folder, or a name in it: not even whether it is there
          out << kept("ls", p)
          code = 1
        elsif File.directory?(p)
          list_dir(out, p, long)
        elsif File.exist?(p)
          out << (long ? "#{size_col(p, false)}  #{p}\n" : "#{p}\n")
        else
          out << "ls: #{p}: no such file or directory\n"
          code = 1
        end
        i += 1
        break if out.bytesize > TOTAL_MAX
      end
      answer(out, code)
    end

    def list_dir(out, dir, long)
      names = FmrbFile.list(dir)
      if names.nil?
        out << "ls: #{dir}: cannot be read\n"
        return
      end
      names = names.sort
      if names.empty?
        out << "(empty directory)\n"
        return
      end
      i = 0
      while i < names.size
        n = names[i]
        i += 1
        if long
          d = n.end_with?("/")
          out << "#{size_col(RShell.join(dir, d ? n[0, n.length - 1] : n), d)}  #{n}\n"
        else
          out << "#{n}\n"
        end
        break if out.bytesize > TOTAL_MAX
      end
    end

    # ls -l's first column: a file's size, <dir>, or - for a secret's.
    def size_col(p, dir)
      return RShell.pad("<dir>", 10) if dir
      return RShell.pad("-", 10) if RShell.secret?(p)
      RShell.pad(FmrbFile.size(p).to_s, 10)
    end

    def cmd_cat(args)
      return answer("usage: cat <file>\n", 2) if args.size != 1
      p = file_arg("cat", args[0])
      return answer(p[1], 1) if p[0].nil?
      path = p[0]
      size = FmrbFile.size(path).to_i
      first = size > 0 ? read_at(path, 0, CHUNK) : ""
      if first.index("\0")
        return answer("cat: #{path}: not text (#{size} bytes)\n", 1)
      end
      @src = path
      @src_size = size
      page = file_page(0)
      { "out" => page[0], "code" => 0, "cwd" => @cwd, "more" => page[1], "cut" => @cut && page[1].nil? }
    end

    # head (last: tail): the first or the last n lines.
    def cmd_head(args, last)
      name = last ? "tail" : "head"
      n = LINES_DEFAULT
      rest = []
      i = 0
      while i < args.size
        a = args[i]
        if a == "-n" && i + 1 < args.size
          n = int_arg(args[i + 1])
          return answer("#{name}: -n: not a number of lines: #{args[i + 1]}\n", 2) if n.nil?
          i += 2
          next
        end
        rest << a
        i += 1
      end
      return answer("usage: #{name} [-n N] <file>\n", 2) if rest.size != 1
      p = file_arg(name, rest[0])
      return answer(p[1], 1) if p[0].nil?
      answer(last ? tail_lines(p[0], n) : head_lines(p[0], n), 0)
    end

    def cmd_stat(args)
      return answer("usage: stat <path>\n", 2) if args.size != 1
      p = RShell.resolve(@cwd, args[0])
      return answer(kept("stat", p), 1) if RShell.secret?(p)
      if File.directory?(p)
        out = "#{p}\n  type: directory\n"
      elsif File.exist?(p)
        out = "#{p}\n  type: file\n  size: #{FmrbFile.size(p)} bytes\n"
      else
        return answer("stat: #{p}: no such file or directory\n", 1)
      end
      out << "  read-only: #{FmrbFile.path_read_only?(p) ? 'yes' : 'no'}\n"
      answer(out, 0)
    end

    def cmd_df
      out = "      FREE  WHERE\n"
      i = 0
      while i < MOUNTS.size
        m = MOUNTS[i]
        i += 1
        next unless File.directory?(m)
        f = FmrbFile.free_bytes(m)
        out << "#{RShell.pad(f.nil? ? '?' : RShell.kb(f), 10)}  #{m}\n"
      end
      answer(out, 0)
    end

    def cmd_free
      out = ""
      h = FmrbApp.heap_info
      if h.is_a?(Hash)
        out << "heap: #{RShell.kb(h[:free])} free of #{RShell.kb(h[:total])} " \
               "(lowest #{RShell.kb(h[:min_free])}, largest block #{RShell.kb(h[:largest_block])})\n"
        it = h[:iram_total].to_i
        out << "internal RAM: #{RShell.kb(h[:iram_free])} free of #{RShell.kb(it)}\n" if it > 0
      end
      sp = FmrbApp.sys_pool_info
      if sp.is_a?(Hash)
        out << "system pool: #{RShell.kb(sp[:used])} used of #{RShell.kb(sp[:total])}\n"
      end
      u = FmrbApp.pool_used
      out << "this app's pool: #{RShell.kb(u)} used (#{FmrbApp.pool_usage}%)\n" if u.to_i >= 0
      answer(out, 0)
    end

    def cmd_ps
      out = " PID TYPE    STATE     ENGINE NAME\n"
      procs = FmrbApp.ps || []
      me = FmrbApp.pid
      i = 0
      while i < procs.size
        pr = procs[i]
        i += 1
        state = STATE_NAMES[pr[:state]] || "?"
        type = TYPE_NAMES[pr[:type]] || "?"
        engine = ENGINE_NAMES[pr[:vm_type]] || "?"
        mark = pr[:id] == me ? " (this)" : ""
        out << "  #{pr[:id]}  #{type.ljust(7)} #{state.ljust(9)} #{engine.ljust(6)} #{pr[:name]}#{mark}\n"
      end
      answer(out, 0)
    end

    def cmd_ver
      out = "Family mruby #{FmrbConst::OS_VERSION} (graphics-audio #{FmrbConst::GA_VERSION}, link #{FmrbConst::LINK_VERSION})\n"
      out << "board: #{FmrbConst::BOARD} (#{FmrbConst::HW_FAMILY}), chip #{FmrbConst::CHIP_MODEL}\n"
      out << "built: #{FmrbConst::BUILD_DATE}, ESP-IDF #{FmrbConst::IDF_VERSION}\n"
      out << "remote shell: #{PROTO}\n"
      answer(out, 0)
    end

    def cmd_help
      out = ""
      i = 0
      while i < TABLE.size
        r = TABLE[i]
        i += 1
        note = r[1] == WRITE ? " (changes the machine; not in this version)" : ""
        out << "  #{r[2].ljust(20)} #{r[3]}#{note}\n"
      end
      answer(out, 0)
    end

    # ---- inside ----

    # The first answer of a command whose output is text: at most OUT_MAX
    # bytes of it, the rest kept for more.
    def answer(text, code)
      t = text.to_s
      if t.bytesize > TOTAL_MAX
        t = t.byteslice(0, RShell.utf8_cut(t, TOTAL_MAX)).to_s
        t << cut_note
        @cut = true
      end
      @text = t
      @src = nil
      page = text_page(0)
      { "out" => page[0], "code" => code, "cwd" => @cwd, "more" => page[1], "cut" => @cut && page[1].nil? }
    end

    def cut_note
      "\n(the output is cut at #{TOTAL_MAX / 1024} KB)\n"
    end

    # [the text from pos, the next pos or nil]
    def text_page(pos)
      t = @text || ""
      return ["", nil] if pos >= t.bytesize
      rest = t.byteslice(pos, t.bytesize - pos).to_s
      k = RShell.utf8_cut(rest, OUT_MAX)
      nxt = pos + k
      [rest.byteslice(0, k).to_s, nxt < t.bytesize ? nxt : nil]
    end

    # cat's: the file's bytes from pos, up to TOTAL_MAX of it.
    def file_page(pos)
      limit = @src_size < TOTAL_MAX ? @src_size : TOTAL_MAX
      return ["", nil] if pos >= limit
      want = limit - pos
      want = OUT_MAX if want > OUT_MAX
      s = read_at(@src, pos, want)
      k = s.bytesize
      # a character cut in two at the end goes with the next piece (or,
      # at the limit, is left out)
      k = RShell.utf8_whole(s) if pos + k < @src_size
      k = s.bytesize if k == 0
      out = s.byteslice(0, k).to_s
      nxt = pos + k
      return [out, nxt] if nxt < limit && pos + s.bytesize < limit
      if @src_size > TOTAL_MAX
        @cut = true
        out << cut_note
      end
      [out, nil]
    end

    # len bytes of path from pos (fewer at its end), the File kept open
    # for the next read when it follows on.
    def read_at(path, pos, len)
      unless @rd && @rd_path == path && @rd_pos == pos
        close_reader
        @rd = File.open(path, "r")
        @rd_path = path
        @rd_pos = 0
        while @rd_pos < pos
          n = pos - @rd_pos
          n = CHUNK if n > CHUNK
          s = @rd.read(n)
          break if s.nil? || s.empty?
          @rd_pos += s.bytesize
        end
      end
      s = @rd.read(len)
      s = "" if s.nil?
      @rd_pos += s.bytesize
      s
    end

    def close_reader
      @rd.close if @rd
      @rd = nil
      @rd_path = nil
      @rd_pos = 0
    rescue StandardError
      @rd = nil
    end

    # [path, nil] for a file that may be read, [nil, the message] otherwise.
    def file_arg(name, arg)
      p = RShell.resolve(@cwd, arg)
      return [nil, kept(name, p)] if RShell.secret?(p)
      return [nil, "#{name}: #{p}: a directory\n"] if File.directory?(p)
      return [nil, "#{name}: #{p}: no such file\n"] unless File.exist?(p)
      [p, nil]
    end

    def kept(name, p)
      "#{name}: #{p}: kept from the remote shell (keys and passwords)\n"
    end

    def int_arg(s)
      return nil if s.nil? || s.empty? || s.bytesize > 9
      i = 0
      while i < s.bytesize
        c = s.getbyte(i)
        return nil if c < 48 || c > 57
        i += 1
      end
      s.to_i
    end

    # The first n lines (at most TOTAL_MAX bytes).
    def head_lines(path, n)
      out = ""
      return out if n <= 0
      got = 0
      pos = 0
      size = FmrbFile.size(path).to_i
      while pos < size && got < n && out.bytesize <= TOTAL_MAX
        s = read_at(path, pos, CHUNK)
        break if s.empty?
        pos += s.bytesize
        j = 0
        from = 0
        while j < s.bytesize
          if s.getbyte(j) == 10
            got += 1
            if got >= n
              out << s.byteslice(from, j + 1 - from).to_s
              from = -1
              break
            end
          end
          j += 1
        end
        out << s.byteslice(from, s.bytesize - from).to_s if from >= 0
      end
      out
    end

    # The last n lines: the file read to its end a piece at a time, keeping
    # at most about 2 * TOTAL_MAX bytes of its end.
    def tail_lines(path, n)
      return "" if n <= 0
      size = FmrbFile.size(path).to_i
      buf = ""
      pos = 0
      while pos < size
        s = read_at(path, pos, CHUNK)
        break if s.empty?
        pos += s.bytesize
        buf << s
        if buf.bytesize > 2 * TOTAL_MAX
          drop = buf.bytesize - TOTAL_MAX
          buf = buf.byteslice(drop, buf.bytesize - drop).to_s
        end
      end
      # from the end: n line ends (the last one, when the file ends with
      # one, is not counted)
      k = buf.bytesize - 1
      k -= 1 if k >= 0 && buf.getbyte(k) == 10
      seen = 0
      while k >= 0
        if buf.getbyte(k) == 10
          seen += 1
          break if seen >= n
        end
        k -= 1
      end
      buf.byteslice(k + 1, buf.bytesize - k - 1).to_s
    end

    # A head that ends in a blank written with a backslash ("a\ ") is still
    # in its word.
    def escaped_end?(head)
      n = head.bytesize
      count = 0
      k = n - 2
      while k >= 0 && head.getbyte(k) == 92
        count += 1
        k -= 1
      end
      count % 2 == 1
    end

    def command_items(word)
      out = []
      i = 0
      while i < TABLE.size
        name = TABLE[i][0]
        out << name if name.start_with?(word)
        i += 1
      end
      out.sort
    end

    # The names that go on from word (as it was typed, quotes and
    # backslashes read), each as the whole word to put in its place.
    def path_items(word)
      cut = -1
      j = 0
      while j < word.bytesize
        cut = j if word.getbyte(j) == 47
        j += 1
      end
      dirpart = word.byteslice(0, cut + 1).to_s
      base = word.byteslice(cut + 1, word.bytesize - cut - 1).to_s
      dir = RShell.resolve(@cwd, dirpart.empty? ? "." : dirpart)
      return [] if RShell.secret?(dir)
      names = FmrbFile.list(dir)
      return [] if names.nil?
      out = []
      i = 0
      while i < names.size
        nm = names[i]
        i += 1
        out << RShell.escape(dirpart + nm) if nm.start_with?(base)
      end
      out.sort
    end

    def error(s)
      "#{s}\n"
    end
  end

  # The object the transfer app exposes: who may come in, and the one
  # session. app: the TransferApp (node_name).
  class Door
    def initialize(app)
      @app = app
      @s = nil
      @next_id = 1
    end

    attr_reader :s

    def open(from: nil)
      let_in(from)
      now = RShell.now_ms
      cur = @s
      if cur && cur.from != from && now - cur.at < IDLE_MS
        raise ArgumentError, "#{node} has a remote shell open from #{cur.from} " \
                             "(called #{(now - cur.at) / 1000} s ago); try again later"
      end
      if cur
        Log.info("rshell: session #{cur.id} from #{cur.from} replaced")
        cur.close
      end
      @s = Session.new(@next_id, from.to_s, now)
      @next_id += 1
      Log.info("rshell: session #{@s.id} opened from #{from}")
      { "id" => @s.id, "cwd" => @s.cwd,
        "version" => { "os" => FmrbConst::OS_VERSION, "board" => FmrbConst::BOARD, "rshell" => PROTO },
        "commands" => RShell.commands, "out_max" => OUT_MAX, "total_max" => TOTAL_MAX, "idle_ms" => IDLE_MS }
    end

    def exec_read(id, line, from: nil)
      session(id, from).exec(line, false)
    end

    def exec_write(id, line, from: nil)
      session(id, from).exec(line, true)
    end

    def more(id, pos, from: nil)
      session(id, from).more(pos)
    end

    def complete(id, line, pos, from: nil)
      session(id, from).complete(line, pos)
    end

    def read(id, path, pos, len, from: nil)
      session(id, from).read(path, pos, len)
    end

    def close(id, from: nil)
      s = @s
      return false unless s && s.id == id && s.from == from.to_s
      drop("closed by #{from}")
      true
    end

    # The switch from the transfer app (TransferApp#rshell): on, the door
    # is exposed as <node>/transfer/rshell when connected (again on every
    # connect: link_up calls it); off, the session ends and the object is
    # taken away (a call then hears there is no such object).
    def switch(on, link)
      c = link.conn
      if on
        return unless c && link.connected?
        c.expose(OBJECT, self, methods: METHODS, caller: :from, doc: DOC)
        Log.info("rshell: exposed as #{link.node}/transfer/#{OBJECT} (#{FmrbApp.pool_used} bytes of the pool used)")
      else
        drop("the remote shell was turned off")
        c.unexpose(OBJECT) if c && link.connected?
        Log.info("rshell: off")
      end
    rescue => e
      Log.warn("rshell: #{e.message}")
    end

    # Called from the app's update: a session not called for IDLE_MS
    # closes.
    def tick(now)
      s = @s
      drop("not called for #{IDLE_MS / 1000} s") if s && now - s.at >= IDLE_MS
    end

    def drop(why)
      s = @s
      return unless s
      Log.info("rshell: session #{s.id} from #{s.from} ends (#{why})")
      s.close
      @s = nil
    end

    # ---- inside ----

    def node
      @app ? @app.node_name : "this machine"
    end

    # The switch, and the caller in the rshell kind's lists (by hand, from
    # the console), read on every call. Raises the reason.
    def let_in(from)
      p = ::FmrbReceiveConf.parts
      unless ::FmrbReceiveConf.rshell_enabled?(p)
        drop("the remote shell was turned off")
        raise ArgumentError, "#{node} lets no remote shell in (rshell_enabled is off there)"
      end
      h = ::FmrbReceiveConf.hand(p)
      m = ::FmrbReceiveConf.managed(p)
      hand = ::FmrbReceiveConf.hand_senders("rshell", h)
      managed = m ? m["rshell"] : nil
      return if ::FmrbReceiveConf.allows?(hand, from) || ::FmrbReceiveConf.allows?(managed, from)
      raise ArgumentError, "#{node} lets no remote shell in from #{from} (not in rshell in receive.toml, by hand or from the console)"
    end

    def session(id, from)
      let_in(from)
      s = @s
      unless s && s.id == id && s.from == from.to_s
        raise ArgumentError, "no remote shell session #{id} from #{from} on #{node} (closed, or not called for " \
                             "#{IDLE_MS / 1000} s: open again)"
      end
      s.at = RShell.now_ms
      s
    end
  end
end

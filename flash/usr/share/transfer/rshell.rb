# The remote shell's machine side (doc/remote_shell R1, R2): the commands
# that look at the machine and change it, run without a screen in the
# transfer app, which
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
#   exec_write(id, line)    any command, the ones that change the machine
#                           too
#                           -> {"out", "code", "cwd", "more", "cut", "wait"}
#   more(id, pos)           the output from pos on (pos: the "more" of the
#                           last answer; the same pos may be asked again)
#                           -> {"out", "more", "cut", "wait", "code"}
#
# kill and open ask the kernel and answer at once that they asked, with
# "wait" => true and "more" at the end of that text: the result (ended,
# started, refused) comes as the text more returns from there, with "code"
# (0 or 1) once it is in. Until then more answers "" with the same pos and
# "wait" => true (ask again a little later); after WAIT_MS without a
# result it says there was no answer.
#   complete(id, line, pos) -> {"word", "items", "cut"}: what may stand for
#                           the word under pos (pos and word count
#                           characters; pos nil or past the end: the end)
#   read(id, path, pos, len)-> {"data", "pos", "size"}: up to READ_MAX bytes
#                           of a file from pos, as they are (the console's
#                           get, doc/remote_shell R4)
#   write_begin(id, path, size, overwrite)
#                           -> {"path", "size", "pos" => 0, "piece"}: a file
#                           of size bytes to come (the console's put) into a
#                           hidden ".<name>.part" beside path; a file there
#                           is written over only when overwrite is true
#   write_chunk(id, pos, data) -> the position held now: data is taken at
#                           that position only (a piece asked again before
#                           it is read past, one after it is not taken)
#   write_end(id)           -> {"path", "size"}: all bytes are in; the piece
#                           file gets its name
#   write_cancel(id)        -> true: the piece file goes (also on close, on
#                           another write_begin and after WRITE_IDLE_MS
#                           without a chunk)
#   stop(id)                -> true: a command still at work (a long cp) or
#                           waited for (kill, open) stops (Ctrl-C)
#   close(id)               -> true
#
# The writes are changes: write_begin refuses the secrets, read-only places
# and a file that does not fit, as cp does. A cp or mv of a file larger than
# COPY_SMALL answers that it started ("wait" => true, as kill and open do)
# and copies a piece at a time from the app's update; more says how far it
# is ("progress" => {"done", "total"}) and then how it ended. The copy goes
# into a hidden ".<name>.part" too, renamed when whole.
#
# Refusals (the switch off, a connection that is not checked, a node not
# let in, another session, no such session) are raised: the caller sees an error with the reason. A command
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
# the API keys and /home/asterism with everything in it) are never read nor
# written: a path is folded (".", "..", "//") before it is looked at, and ls
# and the completion name the folder but never show what is in it. They are
# neither the source nor the destination of cp and mv, and rm and mv refuse
# a folder that holds one ("/", /home, /etc, /home/asterism).
module RShell
  PROTO = 3
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
  # cp copies this much at a time
  COPY_CHUNK = 8192
  # cp and mv copy a file up to this size within the call; a larger one a
  # piece at a time from the app's update, for at most STEP_MS an update
  COPY_SMALL = 65536
  STEP_MS = 30
  # put: the largest piece of a write_chunk, and how long a write that stops
  # coming is kept
  WRITE_MAX = 8192
  WRITE_IDLE_MS = 60000
  # the app's update comes quickly for this long after a call
  BUSY_FOR_MS = 2000
  # kill and open: how long the kernel's answer is waited for
  WAIT_MS = 5000
  # FmrbApp.ps's type of an app that may be ended
  TYPE_USER = 2
  APP_EXTS = [".app.rb", ".app.wasm"]
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
    ["cp", WRITE, "cp [-f] <src> <dst>", "copy a file (-f: over one there)"],
    ["mv", WRITE, "mv [-f] <src> <dst>", "move or rename (-f: over a file there)"],
    ["rm", WRITE, "rm [-r] <path...>", "delete files, empty directories (-r: with all in them)"],
    ["mkdir", WRITE, "mkdir <dir...>", "make a directory"],
    ["kill", WRITE, "kill <pid|name>", "end an app"],
    ["open", WRITE, "open <app>", "start an app (.app.rb, .app.wasm)"]
  ]
  OBJECT = "rshell"
  DOC = "the remote shell: commands a line at a time (doc/remote_shell)"
  # What the transfer app exposes (with the arguments' names for the
  # readers that show them: asterism object info, the console)
  METHODS = {
    "open" => { args: [], doc: "a session: its id, where it is, the version and the commands (read / write)" },
    "exec_read" => { args: ["id", "line"], doc: "a command that only looks -> out, code, cwd, more, cut" },
    "exec_write" => { args: ["id", "line"], doc: "any command, the ones that change the machine too" },
    "more" => { args: ["id", "pos"], doc: "the output from pos on (the more of the last answer; wait: ask again)" },
    "complete" => { args: ["id", "line", "pos"], doc: "what may stand for the word under pos -> word, items, cut" },
    "read" => { args: ["id", "path", "pos", "len"], doc: "up to 8 KB of a file from pos -> data, pos, size" },
    "write_begin" => { args: ["id", "path", "size", "overwrite"], doc: "a file to come in pieces -> path, size, pos, piece" },
    "write_chunk" => { args: ["id", "pos", "data"], doc: "a piece at pos -> the position held now" },
    "write_end" => { args: ["id"], doc: "all of it is in: the file gets its name -> path, size" },
    "write_cancel" => { args: ["id"], doc: "the file coming in is dropped" },
    "stop" => { args: ["id"], doc: "stops a command at work or waited for (a long cp, kill, open)" },
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

  # A folded path that holds a secret, or is one: "/", /home, /etc,
  # /home/asterism and what is in it ("/flash/..." is "/" under another
  # name). fmrb_path_contains_secret in fmrb_secret_path.c, which Ruby
  # cannot call, with the same rule.
  def self.holds_secret?(path)
    p = path
    p = p.byteslice(6, p.bytesize - 6).to_s if p == "/flash" || p.start_with?("/flash/")
    p = "/" if p.empty?
    return true if p == "/" || p == "/home" || p == "/etc"
    secret?(p)
  end

  # The byte where a path's last "/" is (-1: none). Bytes, not characters:
  # names may be UTF-8.
  def self.last_slash(path)
    i = path.bytesize - 1
    while i >= 0
      return i if path.getbyte(i) == 47
      i -= 1
    end
    -1
  end

  # The last name of a folded path
  def self.base(path)
    i = last_slash(path)
    i < 0 ? path : path.byteslice(i + 1, path.bytesize - i - 1).to_s
  end

  # The folder of a folded path ("/" for one at the top)
  def self.dir(path)
    i = last_slash(path)
    return "/" if i <= 0
    path.byteslice(0, i).to_s
  end

  # The hidden file a file is written into before it gets its name:
  # ".<name>.part" beside it.
  def self.part(path)
    join(dir(path), ".#{base(path)}.part")
  end

  # The piece file part gets the name dst (written over). nil when it did,
  # else why not.
  def self.place(part, dst)
    begin
      File.rename(part, dst)
    rescue StandardError
      nil
    end
    return nil if File.exist?(dst) && !File.exist?(part)
    File.delete(dst) if File.exist?(dst) && !File.directory?(dst)
    File.rename(part, dst)
    File.exist?(dst) && !File.exist?(part) ? nil : "could not be renamed from #{part}"
  rescue StandardError => e
    e.message.to_s
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

    def initialize(id, from, now, app = nil)
      @id = id
      @from = from
      @app = app        # the TransferApp: kill and open ask the kernel through it
      @at = now
      @cwd = HOME
      @text = nil       # a command's output, at most TOTAL_MAX bytes
      @cut = false      # ... and whether it was cut there
      @src = nil        # cat: the file whose text is the output
      @src_size = 0
      @rd = nil         # the File kept open while the reads follow on
      @rd_path = nil
      @rd_pos = 0
      @wait = nil       # kill / open: {"what", "pid", "name", "path", "at", "stage"};
                        # a long cp: {"what" => "copy", "src", "dst", "part", "pos", "size", ...}
      @code = nil       # ... and the code of its result once it is in
      @w = nil          # put: {"path", "part", "size", "pos", "file", "at"}
    end

    # ---- the calls ----

    # A line. write: whether the commands that change the machine are
    # taken (exec_write).
    def exec(line, write)
      copy_end("another command came", 1) if copying?
      @text = nil
      @src = nil
      @cut = false
      @wait = nil
      @code = nil
      l = line.to_s
      return answer(error("the line is longer than #{LINE_MAX} bytes"), 2) if l.bytesize > LINE_MAX
      w = RShell.words(l)
      return answer("", 0) if w.empty?
      name = w[0]
      args = w[1, w.size - 1] || []
      r = RShell.row(name)
      return answer("#{name}: no such command (help lists them)\n", 127) if r.nil?
      if r[1] == WRITE && !write
        return answer("#{name}: changes the machine; not taken by exec_read (exec_write)\n", 126)
      end
      run(name, args)
    rescue => e
      answer("#{name}: #{e.message}\n", 1)
    end

    # The output from pos on.
    def more(pos)
      p = pos.to_i
      raise ArgumentError, "pos: #{pos.inspect} is not a position" if p < 0
      waited(RShell.now_ms) if @wait
      if @src
        page = file_page(p)
      elsif @text
        page = text_page(p)
      else
        page = ["", nil]
      end
      if @wait && page[1].nil?
        # the kernel's answer (or the copy's end) is not in yet: ask again
        # from the end
        n = @text.to_s.bytesize
        a = { "out" => page[0], "more" => p > n ? p : n, "cut" => false, "wait" => true, "code" => nil }
        a["progress"] = { "done" => @wait["pos"], "total" => @wait["size"] } if copying?
        return a
      end
      { "out" => page[0], "more" => page[1], "cut" => @cut && page[1].nil?, "wait" => false, "code" => @code }
    end

    # A message from the kernel that the transfer app passes on: kill_result,
    # spawn_result, spawn_failed, and "died" (an app ended, app/died).
    def note(data)
      w = @wait
      return unless w && data.is_a?(Hash)
      cmd = data["cmd"].to_s
      if w["what"] == "kill"
        return unless data["pid"] == w["pid"]
        if cmd == "died"
          done("kill: PID #{w['pid']} (#{w['name']}) ended\n", 0)
        elsif cmd == "kill_result"
          if data["ok"] == true
            w["stage"] = "asked"
          else
            done("kill: PID #{w['pid']} (#{w['name']}): refused by the kernel (#{data['info']})\n", 1)
          end
        end
      elsif w["what"] == "open"
        return unless data["app"].to_s == w["path"]
        if cmd == "spawn_result"
          done("open: #{w['path']} started (PID #{data['pid']})\n", 0)
        elsif cmd == "spawn_failed"
          done("open: #{w['path']}: not started (#{data['reason']})\n", 1)
        end
      end
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
      cmd = a.empty? ? "" : a[0][0]
      items = if first then command_items(word)
              elsif cmd == "kill" then app_items(word)
              else path_items(word, cmd == "open")
              end
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
      write_drop("the session ends")
      copy_end("the session ends", 1) if copying?
      @text = nil
      @src = nil
      @wait = nil
      nil
    end

    # Ctrl-C: a long cp stops (what it wrote goes), kill and open are no
    # longer waited for.
    def stop
      w = @wait
      return true unless w
      if copying?
        copy_end("stopped (what was copied is deleted)", 1)
      else
        done("#{w['what']}: no longer waited for (stopped; the kernel may still do it)\n", 1)
      end
      true
    end

    # ---- put: a file coming in pieces ----

    def write_begin(path, size, overwrite)
      write_drop("another write_begin")
      p = RShell.resolve(@cwd, path)
      raise ArgumentError, "size: #{size.inspect} is not a size" unless size.is_a?(Integer) && size >= 0
      if RShell.secret?(p) || RShell.holds_secret?(p)
        raise ArgumentError, "#{p}: kept from the remote shell (keys and passwords)"
      end
      d = RShell.dir(p)
      raise ArgumentError, "#{d}: no such directory" unless File.directory?(d)
      raise ArgumentError, "#{p}: a directory there" if File.directory?(p)
      raise ArgumentError, "#{p}: read-only here" if FmrbFile.path_read_only?(p)
      old = 0
      if File.exist?(p)
        raise ArgumentError, "#{p}: already there (overwrite to write over it)" unless overwrite == true
        old = FmrbFile.size(p).to_i
      end
      free = FmrbFile.free_bytes(d)
      if free && size > free + old
        raise ArgumentError, "#{p}: #{size} bytes do not fit (#{free} bytes free)"
      end
      part = RShell.part(p)
      f = File.open(part, "w")
      @w = { "path" => p, "part" => part, "size" => size, "pos" => 0, "file" => f, "at" => RShell.now_ms }
      Log.info("rshell: put #{p} (#{size} bytes) begins")
      { "path" => p, "size" => size, "pos" => 0, "piece" => WRITE_MAX }
    end

    def write_chunk(pos, data)
      w = writing
      raise ArgumentError, "pos: #{pos.inspect} is not a position" unless pos.is_a?(Integer) && pos >= 0
      s = data.to_s
      raise ArgumentError, "a piece is at most #{WRITE_MAX} bytes" if s.bytesize > WRITE_MAX
      cur = w["pos"]
      w["at"] = RShell.now_ms
      # a piece after the position (one went missing), or one already in
      return cur if pos > cur || pos + s.bytesize <= cur
      s = s.byteslice(cur - pos, s.bytesize - (cur - pos)).to_s if pos < cur
      if cur + s.bytesize > w["size"]
        write_drop("a piece past its size")
        raise ArgumentError, "#{w['path']}: the piece goes past #{w['size']} bytes"
      end
      begin
        w["file"].write(s)
      rescue StandardError => e
        write_drop("cannot write")
        raise ArgumentError, "#{w['path']}: cannot write at #{cur} (#{e.message})"
      end
      w["pos"] = cur + s.bytesize
    end

    def write_end
      w = writing
      p = w["path"]
      raise ArgumentError, "#{p}: #{w['pos']} of #{w['size']} bytes are in" if w["pos"] != w["size"]
      f = w["file"]
      w["file"] = nil
      f.close
      got = FmrbFile.size(w["part"]).to_i
      if got != w["size"]
        write_drop("the size does not match")
        raise ArgumentError, "#{p}: #{got} bytes were written, not #{w['size']}"
      end
      why = RShell.place(w["part"], p)
      @w = nil
      if why
        File.delete(w["part"]) if File.exist?(w["part"])
        raise ArgumentError, "#{p}: #{why}"
      end
      Log.info("rshell: put #{p} (#{got} bytes)")
      { "path" => p, "size" => got }
    end

    def write_cancel
      write_drop("cancelled")
      true
    end

    # A put not gone on with for WRITE_IDLE_MS goes.
    def write_tick(now)
      w = @w
      write_drop("no piece for #{WRITE_IDLE_MS / 1000} s") if w && now - w["at"] >= WRITE_IDLE_MS
    end

    def writing
      w = @w
      raise ArgumentError, "no file is coming in (write_begin first; it may have been dropped)" unless w
      w
    end

    def write_drop(why)
      w = @w
      return unless w
      @w = nil
      begin
        w["file"].close if w["file"]
      rescue StandardError
        nil
      end
      File.delete(w["part"]) if File.exist?(w["part"])
      Log.info("rshell: put #{w['path']} dropped at #{w['pos']} of #{w['size']} bytes (#{why})")
    rescue StandardError => e
      Log.warn("rshell: #{e.message}")
    end

    # ---- a long cp, a piece at a time ----

    def copying?
      w = @wait
      w ? w["what"] == "copy" : false
    end

    # Copies for at most STEP_MS (the app's update). True while it goes on.
    def step(now)
      return false unless copying?
      w = @wait
      t0 = RShell.now_ms
      while w["pos"] < w["size"]
        s = w["in"].read(COPY_CHUNK)
        break if s.nil? || s.empty?
        w["out"].write(s)
        w["pos"] += s.bytesize
        break if RShell.now_ms - t0 >= STEP_MS
      end
      return true if w["pos"] < w["size"] && RShell.now_ms - t0 >= STEP_MS
      copy_finish
      false
    rescue StandardError => e
      copy_end(e.message.to_s, 1) if copying?
      false
    end

    def copy_start(src, dst, move, size)
      part = RShell.part(dst)
      i = File.open(src, "r")
      begin
        o = File.open(part, "w")
      rescue StandardError => e
        i.close
        raise e
      end
      name = move ? "mv" : "cp"
      @wait = { "what" => "copy", "name" => name, "src" => src, "dst" => dst, "part" => part, "pos" => 0,
                "size" => size, "in" => i, "out" => o, "move" => move }
      waiting("#{name}: #{src} -> #{dst}: copying #{RShell.kb(size)}\n")
    end

    def copy_finish
      w = @wait
      copy_close(w)
      got = FmrbFile.size(w["part"]).to_i
      return copy_end("#{got} bytes were copied, not #{w['size']}", 1) if got != w["size"]
      why = RShell.place(w["part"], w["dst"])
      return copy_end(why, 1) if why
      if w["move"]
        File.delete(w["src"])
        return done("mv: #{w['src']} -> #{w['dst']}: copied, but #{w['src']} could not be deleted\n", 1) if File.exist?(w["src"])
        return done("mv: #{w['src']} -> #{w['dst']}\n", 0)
      end
      done("cp: #{w['src']} -> #{w['dst']} (#{got} bytes)\n", 0)
    end

    # The copy ends without its file (why), its piece file deleted.
    def copy_end(why, code)
      w = @wait
      copy_close(w)
      File.delete(w["part"]) if File.exist?(w["part"])
      done("#{w['name']}: #{w['src']} -> #{w['dst']}: #{why}\n", code)
    rescue StandardError => e
      @wait = nil
      Log.warn("rshell: #{e.message}")
    end

    def copy_close(w)
      f = w["out"]
      w["out"] = nil
      f.close if f
      f = w["in"]
      w["in"] = nil
      f.close if f
    rescue StandardError
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
      when "cp" then cmd_cp(args, false)
      when "mv" then cmd_cp(args, true)
      when "rm" then cmd_rm(args)
      when "mkdir" then cmd_mkdir(args)
      when "kill" then cmd_kill(args)
      when "open" then cmd_open(args)
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
        note = r[1] == WRITE ? " (changes the machine)" : ""
        out << "  #{r[2].ljust(20)} #{r[3]}#{note}\n"
      end
      answer(out, 0)
    end

    # ---- the commands that change the machine ----

    # cp (move: mv) [-f] <src> <dst>: dst a folder puts it in there under
    # its name. A file there is not written over without -f. A file is
    # copied a COPY_CHUNK at a time; mv renames, or copies and deletes when
    # the two are on different storage.
    def cmd_cp(args, move)
      name = move ? "mv" : "cp"
      a = flags(args, "f")
      return answer("usage: #{name} [-f] <src> <dst>\n", 2) if a.nil? || a[1].size != 2
      force = !a[0].index("f").nil?
      src = RShell.resolve(@cwd, a[1][0])
      return answer(kept(name, src), 1) if RShell.secret?(src)
      return answer("#{name}: #{src}: no such file or directory\n", 1) unless File.exist?(src)
      dir = File.directory?(src)
      return answer("#{name}: #{src}: a directory (cp copies files)\n", 1) if dir && !move
      return answer(kept(name, src), 1) if move && RShell.holds_secret?(src)
      return answer(read_only(name, src), 1) if move && FmrbFile.path_read_only?(src)
      dst = RShell.resolve(@cwd, a[1][1])
      dst = RShell.join(dst, RShell.base(src)) if File.directory?(dst)
      return answer(kept(name, dst), 1) if RShell.secret?(dst) || RShell.holds_secret?(dst)
      return answer("#{name}: #{src} and #{dst} are the same\n", 1) if src == dst
      if dir && dst.start_with?("#{src}/")
        return answer("#{name}: #{dst}: inside #{src}\n", 1)
      end
      unless File.directory?(RShell.dir(dst))
        return answer("#{name}: #{RShell.dir(dst)}: no such directory\n", 1)
      end
      return answer(read_only(name, dst), 1) if FmrbFile.path_read_only?(dst)
      if File.exist?(dst)
        return answer("#{name}: #{dst}: a directory there\n", 1) if File.directory?(dst)
        return answer("#{name}: #{dst}: already there (-f to write over it)\n", 1) unless force
      end
      size = dir ? 0 : FmrbFile.size(src).to_i
      if move
        why = move_path(src, dst, dir, size)
        return copy_start(src, dst, true, size) if why == :copy
        return answer("mv: #{src} -> #{dst}: #{why}\n", 1) if why
        Log.info("rshell: mv #{src} #{dst}")
        return answer("mv: #{src} -> #{dst}\n", 0)
      end
      return copy_start(src, dst, false, size) if size > COPY_SMALL
      n = copy_file(src, dst)
      return answer("cp: #{src} -> #{dst}: #{n}\n", 1) if n.is_a?(String)
      Log.info("rshell: cp #{src} #{dst} (#{n} bytes)")
      answer("cp: #{src} -> #{dst} (#{n} bytes)\n", 0)
    end

    # nil when it moved, :copy when it is to be copied a piece at a time
    # (a large file on another storage), else why not.
    def move_path(src, dst, dir, size)
      File.delete(dst) if File.exist?(dst)
      ok = begin
        File.rename(src, dst)
        File.exist?(dst) && !File.exist?(src)
      rescue StandardError
        false
      end
      return nil if ok
      return "cannot be renamed there" if dir
      return :copy if size > COPY_SMALL
      # another storage (/home and the SD card): copied, then deleted
      n = copy_file(src, dst)
      return n if n.is_a?(String)
      File.delete(src)
      File.exist?(src) ? "copied, but #{src} could not be deleted" : nil
    rescue StandardError => e
      e.message
    end

    # The bytes copied, or why not (a String); a part written is deleted.
    def copy_file(src, dst)
      close_reader
      n = 0
      i = nil
      o = nil
      begin
        i = File.open(src, "r")
        o = File.open(dst, "w")
        while true
          s = i.read(COPY_CHUNK)
          break if s.nil? || s.empty?
          o.write(s)
          n += s.bytesize
        end
      rescue StandardError => e
        why = e.message.to_s
      end
      o.close if o
      i.close if i
      if why
        File.delete(dst) if File.exist?(dst)
        return why.empty? ? "not written" : why
      end
      n
    rescue StandardError => e
      e.message.to_s
    end

    # rm [-r] <path...>: files, empty directories, and with -r a directory
    # with all in it.
    def cmd_rm(args)
      a = flags(args, "rf")
      return answer("usage: rm [-r] <path...>\n", 2) if a.nil? || a[1].empty?
      deep = !a[0].index("r").nil?
      out = ""
      code = 0
      i = 0
      while i < a[1].size
        p = RShell.resolve(@cwd, a[1][i])
        i += 1
        why = rm_one(p, deep)
        if why
          out << why
          code = 1
        end
      end
      answer(out, code)
    end

    # nil when p went, else the line saying why not.
    def rm_one(p, deep)
      return kept("rm", p) if RShell.holds_secret?(p)
      return "rm: #{p}: no such file or directory\n" unless File.exist?(p)
      return read_only("rm", p) if FmrbFile.path_read_only?(p)
      unless File.directory?(p)
        File.delete(p)
        return File.exist?(p) ? "rm: #{p}: could not be deleted\n" : nil
      end
      names = FmrbFile.list(p)
      return "rm: #{p}: cannot be read\n" if names.nil?
      if !names.empty? && !deep
        return "rm: #{p}: a directory with things in it (rm -r)\n"
      end
      left = rm_tree(p)
      left ? "rm: #{left}: could not be deleted\n" : nil
    rescue StandardError => e
      "rm: #{p}: #{e.message}\n"
    end

    # Deletes dir with all in it, deepest first, without recursion (a stack
    # of the directories still to empty). nil when it went, else the path
    # that would not.
    def rm_tree(dir)
      stack = [dir]
      while !stack.empty?
        d = stack[stack.size - 1]
        names = FmrbFile.list(d)
        return d if names.nil?
        sub = nil
        i = 0
        while i < names.size
          n = names[i]
          i += 1
          if n.end_with?("/")
            sub = RShell.join(d, n[0, n.length - 1])
            break
          end
          f = RShell.join(d, n)
          File.delete(f)
          return f if File.exist?(f)
        end
        if sub
          stack << sub
          next
        end
        Dir.rmdir(d)
        return d if File.exist?(d)
        stack.pop
      end
      nil
    end

    def cmd_mkdir(args)
      return answer("usage: mkdir <dir...>\n", 2) if args.empty?
      out = ""
      code = 0
      i = 0
      while i < args.size
        p = RShell.resolve(@cwd, args[i])
        i += 1
        why = mkdir_one(p)
        if why
          out << why
          code = 1
        end
      end
      answer(out, code)
    end

    def mkdir_one(p)
      return kept("mkdir", p) if RShell.secret?(p)
      return "mkdir: #{p}: already there\n" if File.exist?(p)
      return "mkdir: #{RShell.dir(p)}: no such directory\n" unless File.directory?(RShell.dir(p))
      return read_only("mkdir", p) if FmrbFile.path_read_only?(p)
      Dir.mkdir(p)
      File.directory?(p) ? nil : "mkdir: #{p}: not made\n"
    rescue StandardError => e
      "mkdir: #{p}: #{e.message}\n"
    end

    # kill <pid|name>: the kernel asks the app to end (as the shell's kill
    # does); the answer says it asked, the result comes with more.
    def cmd_kill(args)
      return answer("usage: kill <pid|name>\n", 2) if args.size != 1
      t = args[0]
      pr = nil
      procs = FmrbApp.ps || []
      num = int_arg(t)
      found = 0
      i = 0
      while i < procs.size
        q = procs[i]
        i += 1
        next if q[:state] == 0
        if (num && q[:id] == num) || (num.nil? && q[:name].to_s == t)
          pr = q
          found += 1
        end
      end
      return answer("kill: #{t}: no such app (ps lists them)\n", 1) if pr.nil?
      return answer("kill: #{found} apps are named #{t}: give the PID (ps)\n", 1) if found > 1
      nm = pr[:name].to_s
      return answer("kill: #{nm}: the transfer app holds this session\n", 1) if pr[:id] == FmrbApp.pid
      if pr[:type] != TYPE_USER
        return answer("kill: #{nm}: a #{TYPE_NAMES[pr[:type]] || '?'} task, not ended from here\n", 1)
      end
      return answer("kill: the kernel cannot be reached\n", 1) unless ask_kernel({ "cmd" => "kill", "pid" => pr[:id] })
      @wait = { "what" => "kill", "pid" => pr[:id], "name" => nm, "at" => RShell.now_ms, "stage" => "sent" }
      waiting("kill: asked the kernel to end PID #{pr[:id]} (#{nm})\n")
    end

    # open <app>: the kernel starts it; the result comes with more.
    def cmd_open(args)
      return answer("usage: open <app>\n", 2) if args.size != 1
      p = RShell.resolve(@cwd, args[0])
      return answer(kept("open", p), 1) if RShell.secret?(p)
      return answer("open: #{p}: no such file\n", 1) unless File.file?(p)
      return answer("open: #{p}: not an app (#{APP_EXTS.join(', ')})\n", 1) unless app_path?(p)
      return answer("open: the kernel cannot be reached\n", 1) unless ask_kernel({ "cmd" => "spawn", "app_name" => p })
      @wait = { "what" => "open", "path" => p, "at" => RShell.now_ms, "stage" => "sent" }
      waiting("open: asked the kernel to start #{p}\n")
    end

    def app_path?(p)
      i = 0
      while i < APP_EXTS.size
        return true if p.end_with?(APP_EXTS[i])
        i += 1
      end
      false
    end

    def ask_kernel(h)
      return false unless @app
      @app.send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, h) ? true : false
    end

    # The first answer of kill / open: the text, more at its end, wait.
    def waiting(text)
      @text = text
      @src = nil
      Log.info("rshell: #{text.strip}")
      { "out" => text, "code" => 0, "cwd" => @cwd, "more" => text.bytesize, "cut" => false, "wait" => true }
    end

    # WAIT_MS gone without the kernel's answer (a copy is not timed)
    def waited(now)
      w = @wait
      return if w["what"] == "copy" || now - w["at"] < WAIT_MS
      s = WAIT_MS / 1000
      if w["what"] == "kill" && w["stage"] == "asked"
        done("kill: PID #{w['pid']} (#{w['name']}) was asked to end and has not in #{s} s (ps)\n", 1)
      else
        done("#{w['what']}: no answer from the kernel in #{s} s (ps)\n", 1)
      end
    end

    def done(text, code)
      @text = "#{@text}#{text}"
      @wait = nil
      @code = code
      Log.info("rshell: #{text.strip}")
    end

    # [the flags given, the other words] (letters: the flags taken), nil
    # for another flag.
    def flags(args, letters)
      got = ""
      rest = []
      i = 0
      while i < args.size
        a = args[i]
        i += 1
        if a.start_with?("-") && a.bytesize > 1 && rest.empty?
          j = 1
          while j < a.bytesize
            c = a.byteslice(j, 1)
            return nil if letters.index(c).nil?
            got << c
            j += 1
          end
        else
          rest << a
        end
      end
      [got, rest]
    end

    def read_only(name, p)
      "#{name}: #{p}: read-only here\n"
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
      # only the first bytes of a character: left out at the limit (they
      # would reach the caller as a broken one), else sent as they are (a
      # broken sequence, so that the pieces go on)
      if k == 0
        k = pos + s.bytesize >= limit ? 0 : s.bytesize
      end
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

    # The running apps that may be ended (kill), by name.
    def app_items(word)
      out = []
      procs = FmrbApp.ps || []
      me = FmrbApp.pid
      i = 0
      while i < procs.size
        q = procs[i]
        i += 1
        next if q[:state] == 0 || q[:type] != TYPE_USER || q[:id] == me
        n = RShell.escape(q[:name].to_s)
        out << n if n.start_with?(word) && !out.include?(n)
      end
      out.sort
    end

    # The names that go on from word (as it was typed, quotes and
    # backslashes read), each as the whole word to put in its place. apps:
    # only folders and apps (open).
    def path_items(word, apps = false)
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
        next unless nm.start_with?(base)
        next if apps && !nm.end_with?("/") && !app_path?(nm)
        out << RShell.escape(dirpart + nm)
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

    # A message from the kernel for the session's kill / open (TransferApp).
    def note(data)
      @s.note(data) if @s
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
      @s = Session.new(@next_id, from.to_s, now, @app)
      @next_id += 1
      Log.info("rshell: session #{@s.id} opened from #{from}")
      mark(true) unless cur
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

    def write_begin(id, path, size, overwrite, from: nil)
      session(id, from).write_begin(path, size, overwrite)
    end

    def write_chunk(id, pos, data, from: nil)
      session(id, from).write_chunk(pos, data)
    end

    def write_end(id, from: nil)
      session(id, from).write_end
    end

    def write_cancel(id, from: nil)
      session(id, from).write_cancel
    end

    def stop(id, from: nil)
      session(id, from).stop
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
    # closes, a put that stopped coming goes, a long cp goes on. True while
    # the update should come quickly (a copy at work, a call a moment ago).
    def tick(now)
      s = @s
      return false unless s
      if now - s.at >= IDLE_MS
        drop("not called for #{IDLE_MS / 1000} s")
        return false
      end
      s.write_tick(now)
      s.step(now) || now - s.at < BUSY_FOR_MS
    end

    def drop(why)
      s = @s
      return unless s
      Log.info("rshell: session #{s.id} from #{s.from} ends (#{why})")
      s.close
      @s = nil
      mark(false)
    end

    # The mark on the desktop's menu bar while a session is open: the kernel
    # passes it on (rshell_state).
    def mark(on)
      @app.tell_kernel({ "cmd" => "rshell_state", "on" => on }) if @app
    rescue StandardError => e
      Log.warn("rshell: #{e.message}")
    end

    # ---- inside ----

    def node
      @app ? @app.node_name : "this machine"
    end

    def certified?
      @app ? @app.certified? == true : false
    end

    # The switch, the connection, and the caller in the rshell kind's lists
    # (by hand, from the console), read on every call. Raises the reason.
    # The connection must be a checked one (TransferApp#certified?: a tls/
    # router, or the console's door in the browser build), where the caller's
    # name is its certificate's; rshell_uncertified = true by hand takes any
    # (the sim, development checks).
    def let_in(from)
      p = ::FmrbReceiveConf.parts
      unless ::FmrbReceiveConf.rshell_enabled?(p)
        drop("the remote shell was turned off")
        raise ArgumentError, "#{node} lets no remote shell in (rshell_enabled is off there)"
      end
      unless certified? || ::FmrbReceiveConf.rshell_uncertified?(p)
        raise ArgumentError, "#{node} takes the remote shell only over a checked connection (a tls/ router with " \
                             "certificates, or the console's door in the browser build); this one is not"
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

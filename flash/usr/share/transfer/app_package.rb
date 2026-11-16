# FmrbAppPackage: an app as one file on the wire, and its install under
# /app/usr/<name>/ (doc/wasm_apps W6).
#
# The package is a tar (ustar; what tar, CRuby's Gem::Package::TarWriter
# and pack below write) of the app's folder: its files at the top, under
# the names they take in /app/usr/<name>/:
#
#   <name>.app.<ext>     the program: rb (Ruby), lua (Lua), py (Python),
#                        bas (BASIC) or wasm (a wasm app); exactly one
#   <name>.app.toml      its attributes (optional for an .app.rb that carries
#                        them in its "#---fmrb" comment)
#   anything else        its icon (a PNG beside the .icon the .app.toml names,
#                        or a .png), pictures, sounds, data; subfolders too
#
# <name> is the app's name on this machine (letters, digits, _ and -, up to
# 40 bytes). A package made of the folder itself (every entry under
# <name>/) is taken the same. Entries are files and folders only (no links);
# a name may not lead out of the folder (no "/" first, no "..") nor be
# hidden (no "." first); at most FILES_MAX files, PATH_MAX bytes a path.
#
# The install (the transfer app's, transfer.app.rb):
#
#   1. the package is unpacked into a hidden folder beside the place,
#      /app/usr/.<name>.new/ (stage), so that nothing is placed before
#      it is looked at (check)
#   2. check: the program is there, its .app.toml reads, and a wasm app
#      imports only what this firmware gives (FmrbApp._wasm_check, the
#      check of the load: refused with the reason, e.g. an app built with a
#      newer SDK)
#   3. place: the installed one (same name) is taken away, the hidden
#      folder becomes /app/usr/<name>/, and an .app.toml's icon that names
#      a file of the package is written as its path there
#      (app/usr/<name>/<file>). The launcher's index cache is dropped and
#      the desktop told (the kernel's "apps_installed"): the launcher reads
#      /app again.
#
# Sending (pack): the folder of an app (or its program, with the files
# beside it of the same name) into a tar in /tmp.
module FmrbAppPackage
  ROOT = "/app/usr"
  BLOCK = 512
  FILES_MAX = 64
  PATH_MAX = 96
  # reading and writing a file's data, a piece at a time
  PIECE = 4096
  CACHE_PATH = "/var/cache/launcher_index"
  # the program's extension -> the language shown
  LANGS = { "rb" => "Ruby", "lua" => "Lua", "py" => "Python", "bas" => "BASIC", "wasm" => "wasm" }
  EXTS = ["rb", "lua", "py", "bas", "wasm"]
  # wasm apps' language key in their .app.toml -> the name shown
  WASM_LANGS = { "tinygo" => "TinyGo", "rust" => "Rust", "zig" => "Zig", "c" => "C" }

  # The app's name on this machine: letters, digits, _ and -, up to 40 bytes.
  def self.name_ok?(name)
    return false unless name.is_a?(String)
    return false if name.empty? || name.bytesize > 40
    i = 0
    while i < name.bytesize
      c = name.getbyte(i)
      ok = (c >= 97 && c <= 122) || (c >= 65 && c <= 90) || (c >= 48 && c <= 57) || c == 95 || c == 45
      return false unless ok
      i += 1
    end
    true
  end

  def self.dir_of(name)
    "#{ROOT}/#{name}"
  end

  def self.stage_of(name)
    "#{ROOT}/.#{name}.new"
  end

  def self.installed?(name)
    Dir.exist?(dir_of(name))
  end

  # ---- reading a tar ----

  # A path of the package, checked: its parts are names that stay in the
  # folder ("" parts dropped: "a//b", a trailing "/"). -> the parts, or
  # raises ArgumentError.
  def self.parts_of(path)
    raise ArgumentError, "a path of the package is empty" if path.empty?
    raise ArgumentError, "#{path}: longer than #{PATH_MAX} bytes" if path.bytesize > PATH_MAX
    raise ArgumentError, "#{path}: starts with /" if path.getbyte(0) == 47
    raw = path.split("/")
    out = []
    i = 0
    while i < raw.size
      pc = raw[i]
      i += 1
      next if pc.empty? || pc == "."
      unless ::Asterism::Transfer.file_name?(pc)
        raise ArgumentError, "#{path}: not a name in the app's folder (no .., not hidden, 48 bytes a part)"
      end
      out << pc
    end
    raise ArgumentError, "a path of the package is empty" if out.empty?
    out
  end

  # A NUL-ended field of the header.
  def self.field(h, at, n)
    z = 0
    z += 1 while z < n && h.getbyte(at + z) != 0
    h.byteslice(at, z).to_s
  end

  # An octal number of the header (spaces and NULs around it).
  def self.octal(h, at, n)
    v = 0
    seen = false
    i = 0
    while i < n
      c = h.getbyte(at + i)
      i += 1
      if c >= 48 && c <= 55
        v = v * 8 + (c - 48)
        seen = true
      elsif c == 32 || c == 0
        break if seen
      else
        raise ArgumentError, "not a tar (a number of a header is not octal)"
      end
    end
    v
  end

  # Whether a header's checksum holds (the sum of its bytes, the checksum's
  # own eight counted as spaces).
  def self.sum_ok?(h)
    want = octal(h, 148, 8)
    sum = 0
    i = 0
    while i < BLOCK
      sum += (i >= 148 && i < 156) ? 32 : h.getbyte(i)
      i += 1
    end
    sum == want
  end

  def self.zero_block?(h)
    i = 0
    while i < BLOCK
      return false if h.getbyte(i) != 0
      i += 1
    end
    true
  end

  # Reads n bytes of io into out (an IO, or nil to drop them).
  def self.copy(io, n, out)
    left = n
    while left > 0
      want = left < PIECE ? left : PIECE
      s = io.read(want)
      raise ArgumentError, "the package ends inside a file" if s.nil? || s.bytesize == 0
      out.write(s) if out
      left -= s.bytesize
    end
  end

  # Unpacks the tar at path into dir (made; it must not be there): ->
  # {"files" => [[path, size], ...], "bytes"}. Raises ArgumentError with
  # the reason (dir is taken away then).
  def self.unpack(path, dir)
    files = []
    bytes = 0
    io = File.open(path, "r")
    begin
      Dir.mkdir(dir)
      while true
        h = io.read(BLOCK)
        break if h.nil? || h.bytesize == 0
        raise ArgumentError, "not a tar (a header is cut short)" if h.bytesize < BLOCK
        break if zero_block?(h)
        raise ArgumentError, "not a tar (a header's checksum does not hold)" unless sum_ok?(h)
        size = octal(h, 124, 12)
        type = h.getbyte(156)
        name = field(h, 0, 100)
        if field(h, 257, 5) == "ustar"
          pre = field(h, 345, 155)
          name = "#{pre}/#{name}" unless pre.empty?
        end
        pad = (BLOCK - size % BLOCK) % BLOCK
        if type == 120 || type == 103
          # a pax header (another tool's attributes): passed by
          copy(io, size + pad, nil)
          next
        end
        if type == 53
          parts_of(name)
          copy(io, size + pad, nil)
          next
        end
        unless type == 48 || type == 0
          raise ArgumentError, "#{name}: not a file or a folder (links and devices are not taken)"
        end
        parts = parts_of(name)
        raise ArgumentError, "more than #{FILES_MAX} files" if files.size >= FILES_MAX
        make_dirs(dir, parts)
        dest = "#{dir}/#{parts.join('/')}"
        raise ArgumentError, "#{parts.join('/')} is twice in the package" if File.exist?(dest)
        out = File.open(dest, "w")
        begin
          copy(io, size, out)
        ensure
          out.close
        end
        copy(io, pad, nil)
        files << [parts.join("/"), size]
        bytes += size
      end
    ensure
      io.close
    end
    raise ArgumentError, "the package holds no file" if files.empty?
    { "files" => files, "bytes" => bytes }
  rescue ArgumentError => e
    remove_tree(dir)
    raise e
  rescue => e
    remove_tree(dir)
    raise ArgumentError, "cannot unpack (#{e.message})"
  end

  # The folders of a file's path under dir, made where missing.
  def self.make_dirs(dir, parts)
    d = dir
    i = 0
    while i < parts.size - 1
      d = "#{d}/#{parts[i]}"
      Dir.mkdir(d) unless Dir.exist?(d)
      i += 1
    end
  end

  # ---- what the unpacked package is ----

  # Looks at the folder dir unpacked for the app name: -> {"name", "main"
  # (its file name), "ext", "lang" (shown), "label", "toml" (a Hash or nil),
  # "sdk" (a wasm app's SDK version, "" when none), "bytes"}. Raises
  # ArgumentError with the reason the app is refused.
  def self.check(dir, name, files)
    top = Dir.children(dir)
    # a package made of the folder itself: every entry under <name>/
    if top.size == 1 && top[0] == name && Dir.exist?("#{dir}/#{name}")
      lift(dir, name)
      top = Dir.children(dir)
    end
    main = nil
    ext = nil
    i = 0
    while i < top.size
      f = top[i].to_s
      i += 1
      k = 0
      while k < EXTS.size
        if f == "#{name}.app.#{EXTS[k]}"
          raise ArgumentError, "two programs in the package (#{main}, #{f})" if main
          main = f
          ext = EXTS[k]
        end
        k += 1
      end
    end
    raise ArgumentError, "no #{name}.app.rb, .lua, .py, .bas or .wasm at the top of the package" if main.nil?
    toml = nil
    tpath = "#{dir}/#{name}.app.toml"
    if File.exist?(tpath)
      text = File.open(tpath, "r") { |f| f.read }.to_s
      toml = FmrbToml.parse(text)
      raise ArgumentError, "#{name}.app.toml is not TOML (#{FmrbToml.error})" if toml.nil?
    elsif ext != "rb"
      raise ArgumentError, "no #{name}.app.toml (an app other than Ruby carries its attributes in it)"
    end
    lang = LANGS[ext]
    sdk = ""
    if ext == "wasm"
      unless FmrbApp.respond_to?(:_wasm_check)
        raise ArgumentError, "this machine cannot look at wasm apps (its transfer app runs without the check)"
      end
      r = FmrbApp._wasm_check("#{dir}/#{main}")
      sdk = r[1].to_s
      raise ArgumentError, "#{main}: #{r[2]}" unless r[0]
      l = toml ? toml["language"].to_s : ""
      # no language key: the language's icon names it (usr/share/icon/tinygo.icon)
      l = icon_language(toml) if l.empty?
      shown = WASM_LANGS[l]
      lang = "wasm (#{shown || l})" unless l.empty?
    end
    label = toml ? toml["app_screen_name"].to_s : ""
    label = name if label.empty?
    bytes = 0
    i = 0
    while i < files.size
      bytes += files[i][1]
      i += 1
    end
    { "name" => name, "main" => main, "ext" => ext, "lang" => lang, "label" => label, "toml" => toml,
      "sdk" => sdk, "bytes" => bytes, "files" => files.size }
  end

  def self.icon_language(toml)
    icon = toml ? toml["icon"].to_s : ""
    return "" unless icon.start_with?("usr/share/icon/") && icon.end_with?(".icon")
    l = icon.byteslice(15, icon.bytesize - 20).to_s
    WASM_LANGS[l] ? l : ""
  end

  # dir/<name>/* -> dir/* (a package of the folder itself).
  def self.lift(dir, name)
    inner = "#{dir}/#{name}"
    tmp = "#{dir}/.lift"
    File.rename(inner, tmp)
    names = Dir.children(tmp)
    i = 0
    while i < names.size
      File.rename("#{tmp}/#{names[i]}", "#{dir}/#{names[i]}")
      i += 1
    end
    Dir.rmdir(tmp)
  end

  # ---- how it is installed (doc/wasm_apps W6, 2026-11-16 user) ----

  NO_SCREEN = "this board has no screen to ask; set it to auto with a certificate or allow the sender"

  # mode: the setting ("ask" | "auto"); certified: the sender's node ID is
  # bound to its certificate (a tls/ router, or the browser page the
  # console's door connected: TransferApp#certified?); can_ask: there is a
  # desktop to show the question on. -> "auto" (placed without asking),
  # "ask", or "refuse" (no screen to ask on). "auto" holds only for a
  # sender checked by its certificate; anybody else may only call itself by
  # a listed name, so the owner is asked even then.
  def self.install_how(mode, certified, can_ask)
    return "auto" if mode == "auto" && certified
    can_ask ? "ask" : "refuse"
  end

  # ---- placing it ----

  # The checked folder stage becomes /app/usr/<name>/ (the installed one
  # taken away first). -> the program's path there. Raises ArgumentError.
  def self.place(stage, info)
    name = info["name"]
    dest = dir_of(name)
    fix_icon(stage, info)
    remove_tree(dest) if Dir.exist?(dest)
    begin
      File.rename(stage, dest)
    rescue => e
      # a filesystem that does not rename folders: the files one by one
      Log.info("transfer: #{stage} -> #{dest} as a folder: #{e.message}; moving its files")
      move_tree(stage, dest)
    end
    drop_launcher_cache
    "#{dest}/#{info['main']}"
  rescue ArgumentError => e
    raise e
  rescue => e
    raise ArgumentError, "cannot place #{dest} (#{e.message})"
  end

  # An .app.toml's icon that names a file of the package (no "/" in it,
  # or the path in the package) is written as its path on this machine.
  def self.fix_icon(stage, info)
    t = info["toml"]
    return unless t.is_a?(Hash)
    icon = t["icon"].to_s
    return if icon.empty? || icon.start_with?("usr/") || icon.start_with?("app/") || icon.start_with?("/")
    ok = File.exist?("#{stage}/#{icon}")
    if !ok && icon.end_with?(".icon")
      png = "#{icon.byteslice(0, icon.bytesize - 5)}.png"
      ok = File.exist?("#{stage}/#{png}")
    end
    return unless ok
    path = "#{stage}/#{info['name']}.app.toml"
    text = File.open(path, "r") { |f| f.read }.to_s
    line = "icon = \"app/usr/#{info['name']}/#{icon}\""
    lines = text.split("\n")
    i = 0
    done = false
    while i < lines.size
      s = lines[i].strip
      if s.start_with?("[")
        break
      elsif s.start_with?("icon") && s.index("=") && s.byteslice(0, s.index("=")).strip == "icon"
        lines[i] = line
        done = true
        break
      end
      i += 1
    end
    lines.insert(0, line) unless done
    File.open(path, "w") { |f| f.write(lines.join("\n") + "\n") }
  end

  def self.move_tree(from, to)
    Dir.mkdir(to) unless Dir.exist?(to)
    names = Dir.children(from)
    i = 0
    while i < names.size
      a = "#{from}/#{names[i]}"
      b = "#{to}/#{names[i]}"
      if Dir.exist?(a)
        move_tree(a, b)
      else
        File.rename(a, b)
      end
      i += 1
    end
    Dir.rmdir(from)
  end

  # A folder and all in it (nothing when it is not there).
  def self.remove_tree(dir)
    return unless Dir.exist?(dir)
    names = Dir.children(dir)
    i = 0
    while i < names.size
      p = "#{dir}/#{names[i]}"
      if Dir.exist?(p)
        remove_tree(p)
      else
        File.delete(p)
      end
      i += 1
    end
    Dir.rmdir(dir)
  rescue => e
    Log.warn("transfer: cannot remove #{dir}: #{e.message}")
  end

  # Boot trusts the launcher's index without walking /app (launcher.rb):
  # an app added is seen after a rescan, which writes it again.
  def self.drop_launcher_cache
    File.delete(CACHE_PATH) if File.exist?(CACHE_PATH)
  rescue => e
    Log.warn("transfer: cannot drop the launcher cache: #{e.message}")
  end

  # Hidden folders left in /app/usr/ by an install that did not end (the
  # machine stopped): taken away when the transfer app starts.
  def self.sweep
    return unless Dir.exist?(ROOT)
    names = Dir.children(ROOT)
    i = 0
    while i < names.size
      n = names[i].to_s
      i += 1
      next unless n.getbyte(0) == 46 && n.end_with?(".new")
      Log.info("transfer: #{ROOT}/#{n} left from an install that did not end; removed")
      remove_tree("#{ROOT}/#{n}")
    end
  rescue => e
    Log.warn("transfer: #{ROOT}: #{e.message}")
  end

  # ---- making one (sending) ----

  # The files of the app at src: a folder (its files, subfolders too), or
  # a program (<dir>/<name>.app.<ext>: it and the files beside it whose
  # names start with "<name>." -- its .app.toml, its icon). -> [name,
  # [[path in the package, file], ...]]. Raises ArgumentError.
  def self.files_of(src)
    s = src.to_s
    s = s.byteslice(0, s.bytesize - 1).to_s while s.bytesize > 1 && s.end_with?("/")
    if Dir.exist?(s)
      name = s.split("/").last.to_s
      out = []
      walk(s, "", out)
      return [name, out]
    end
    raise ArgumentError, "no such file or folder: #{s}" unless File.exist?(s)
    slash = s.rindex("/")
    dir = slash ? s.byteslice(0, slash).to_s : "."
    dir = "/" if dir.empty?
    base = s.split("/").last.to_s
    at = base.index(".app.")
    raise ArgumentError, "#{base} is not an app (<name>.app.rb, .lua, .py, .bas, .wasm)" if at.nil? || at == 0
    name = base.byteslice(0, at).to_s
    out = []
    names = Dir.children(dir)
    i = 0
    while i < names.size
      n = names[i].to_s
      i += 1
      p = dir == "/" ? "/#{n}" : "#{dir}/#{n}"
      next unless n.start_with?("#{name}.") && !Dir.exist?(p)
      out << [n, p]
    end
    [name, out]
  end

  def self.walk(dir, rel, out)
    names = Dir.children(dir)
    i = 0
    while i < names.size
      n = names[i].to_s
      i += 1
      next if n.getbyte(0) == 46
      p = "#{dir}/#{n}"
      r = rel.empty? ? n : "#{rel}/#{n}"
      if Dir.exist?(p)
        walk(p, r, out)
      else
        raise ArgumentError, "more than #{FILES_MAX} files in #{dir}" if out.size >= FILES_MAX
        out << [r, p]
      end
    end
  end

  # The app at src packed into out (a tar). -> {"name", "files", "size"}.
  def self.pack(src, out)
    nf = files_of(src)
    name = nf[0]
    files = nf[1]
    raise ArgumentError, "#{name}: not an app's name (letters, digits, _ and -)" unless name_ok?(name)
    raise ArgumentError, "#{src}: no files" if files.empty?
    io = File.open(out, "w")
    begin
      i = 0
      while i < files.size
        rel = files[i][0]
        path = files[i][1]
        raise ArgumentError, "#{rel}: longer than 100 bytes" if rel.bytesize > 100
        size = File.size(path)
        io.write(header(rel, size))
        src_io = File.open(path, "r")
        begin
          copy(src_io, size, io)
        ensure
          src_io.close
        end
        pad = (BLOCK - size % BLOCK) % BLOCK
        io.write("\0" * pad) if pad > 0
        i += 1
      end
      io.write("\0" * (BLOCK * 2))
    ensure
      io.close
    end
    { "name" => name, "files" => files.size, "size" => File.size(out) }
  end

  # A ustar header of a file.
  def self.header(name, size)
    h = name + "\0" * (100 - name.bytesize)
    h << "0000644\0" << "0000000\0" << "0000000\0"
    h << octal_field(size, 12) << octal_field(0, 12)
    h << "        " << "0"
    h << "\0" * 100
    h << "ustar\0" << "00"
    h << "\0" * (BLOCK - h.bytesize)
    sum = 0
    i = 0
    while i < BLOCK
      sum += h.getbyte(i)
      i += 1
    end
    s = sum.to_s(8)
    s = "0" * (6 - s.bytesize) + s if s.bytesize < 6
    h.byteslice(0, 148).to_s + s + "\0 " + h.byteslice(156, BLOCK - 156).to_s
  end

  def self.octal_field(v, n)
    s = v.to_s(8)
    "0" * (n - 1 - s.bytesize) + s + "\0"
  end
end

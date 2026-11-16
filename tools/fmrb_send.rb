#!/usr/bin/env ruby
# frozen_string_literal: true
#
# fmrb_send.rb - send a file from the PC to a Family mruby machine over
# Asterism (doc/asterism_file_transfer T2): the native program, the
# simulation, the browser build or a board, whichever node is named. The
# file goes as the kind "file" to <node>/transfer/inbox and is placed under
# that machine's /home/inbox/ (--sd: its SD card's /sd/inbox/; a machine
# with no card refuses it); it must receive (enabled = true in its
# receive.toml, or areceive on there) and take files from this PC's node
# ([file] from in its /home/asterism/receive.toml).
#
#   ruby tools/fmrb_send.rb <node> <file>
#        [--router tcp/127.0.0.1:7447]   the router that machine is on
#        [--node fmrb-send]              this PC's node ID (the receiver's
#                                        receive.toml names it)
#        [--overwrite]                   replace a file of that name there
#                                        (default: it stays, and this one
#                                        gets a name of its own, foo(1).txt)
#        [--name NAME]                   the name there (default: the file's)
#        [--sd]                          to the SD card's /sd/inbox/ there
#        [--app]                         <file> is an app (its folder, or its
#                                        program <name>.app.<ext> with the
#                                        files of its name beside it): it is
#                                        packed and installed there as
#                                        /app/usr/<name>/ (doc/wasm_apps W6),
#                                        when that machine takes apps from
#                                        this node ([app] from) and its owner
#                                        says yes (unless install = "auto"
#                                        there); --overwrite replaces the
#                                        app of that name there
#        [--credentials ROW.tar]         connect with a member row's
#                                        certificate (the console's .tar):
#                                        its router and its name, unless
#                                        --router / --node say otherwise; a
#                                        cloud router's ACL then lets it call
#                                        the inboxes of whom chose it (T6, T8)
#
# It prints the progress and the name the file was placed under; the exit
# status is 0 when placed. Ctrl+C stops it, and the machine drops what it
# holds of the file. A dropped connection or a receiver that stops for a
# while is gone on from where the receiver stands (Asterism::Transfer, the
# same code as the machines').
#
# Asterism (CRuby) is taken from ASTERISM_DIR (a working checkout of the
# gem), else the pinned one of this tree (vendor/asterism, rake
# asterism:setup), else the installed gem; asterism-zenoh is the installed
# gem.
require "optparse"
require "tmpdir"

module FmrbSend
  CORE = File.expand_path("..", __dir__)
  DEFAULT_ROUTER = "tcp/127.0.0.1:7447"
  DEFAULT_NODE = "fmrb-send"
  APP = "fmrb_send"

  def self.load_asterism
    [ENV["ASTERISM_DIR"], File.join(CORE, "vendor", "asterism")].compact.each do |d|
      lib = File.join(d, "lib")
      next unless File.exist?(File.join(lib, "asterism", "transfer.rb"))
      $LOAD_PATH.unshift(lib)
      break
    end
    require "asterism"
    require "asterism/transfer"
  end

  def self.main(argv)
    opt = { router: DEFAULT_ROUTER, node: DEFAULT_NODE, overwrite: false, name: nil, sd: false, app: false }
    parser = OptionParser.new do |o|
      o.banner = "usage: fmrb_send.rb <node> <file> [--router LOC] [--node ID] [--overwrite] [--name NAME] [--sd] " \
                 "[--credentials ROW.tar]"
      o.on("--router LOC") { |v| opt[:router] = v }
      o.on("--node ID") { |v| opt[:node] = v }
      o.on("--overwrite") { opt[:overwrite] = true }
      o.on("--name NAME") { |v| opt[:name] = v }
      o.on("--sd") { opt[:sd] = true }
      o.on("--app") { opt[:app] = true }
      o.on("--credentials TAR") { |v| opt[:credentials] = v }
    end
    given = argv.join(" ")
    parser.parse!(argv)
    abort parser.banner unless argv.size == 2
    to, path = argv
    abort "fmrb_send: no such file: #{path}" unless File.file?(path) || (opt[:app] && File.directory?(path))
    load_asterism
    if opt[:credentials]
      args = given.include?("--router") ? [opt[:router]] : []
      kw = given.include?("--node") ? { node: opt[:node] } : {}
      net = Asterism.connect(*args, credentials: opt[:credentials], app: APP, **kw)
    else
      net = Asterism.connect(opt[:router], node: opt[:node], app: APP)
    end
    begin
      ok = if opt[:app]
             send_app(net, to, path, overwrite: opt[:overwrite])
           else
             send_file(net, to, path, overwrite: opt[:overwrite], name: opt[:name], sd: opt[:sd])
           end
    ensure
      net.close
    end
    ok
  end

  # ---- an app to install (doc/wasm_apps W6) ----

  EXTS = %w[rb lua py bas wasm].freeze
  # the owner there has 60 s to answer (the transfer app's ASK_MS)
  ANSWER_S = 75
  STATUS_EVERY_S = 0.5

  # The files of an app: its folder (subfolders too, no hidden files), or
  # its program and the files beside it whose names start with "<name>.".
  # -> [name, [[path in the package, file], ...]]
  def self.app_files(path)
    if File.directory?(path)
      dir = File.expand_path(path)
      name = File.basename(dir)
      files = Dir.glob("**/*", base: dir).sort.reject { |r| File.directory?(File.join(dir, r)) }
      return [name, files.map { |r| [r, File.join(dir, r)] }]
    end
    base = File.basename(path)
    name, ext = base.split(".app.", 2)
    abort "fmrb_send: #{base} is not an app (<name>.app.#{EXTS.join('|')})" unless ext && EXTS.include?(ext)
    dir = File.dirname(File.expand_path(path))
    files = Dir.children(dir).sort.select { |n| n.start_with?("#{name}.") && File.file?(File.join(dir, n)) }
    [name, files.map { |n| [n, File.join(dir, n)] }]
  end

  # The app at path packed into a tar (ustar, what the machines read:
  # /usr/share/transfer/app_package.rb). -> [name, the tar's path, files]
  def self.pack_app(path, out)
    require "rubygems/package"
    name, files = app_files(path)
    abort "fmrb_send: #{name}: not an app's name (letters, digits, _ and -)" unless name.match?(/\A[A-Za-z0-9_-]{1,40}\z/)
    abort "fmrb_send: #{path}: no files" if files.empty?
    File.open(out, "wb") do |io|
      Gem::Package::TarWriter.new(io) do |tar|
        files.each do |rel, file|
          data = File.binread(file)
          tar.add_file_simple(rel, 0o644, data.bytesize) { |f| f.write(data) }
        end
      end
    end
    [name, out, files.size]
  end

  # The app at path installed on <to>: sent as the kind "app", then status
  # until it is installed there or refused. -> true when installed.
  def self.send_app(net, to, path, overwrite: false, out: $stdout, answer_s: ANSWER_S)
    Dir.mktmpdir("fmrb_send") do |tmp|
      name, tar, n = pack_app(path, File.join(tmp, "app.tar"))
      meta = { "app" => name, "files" => n }
      meta["overwrite"] = true if overwrite
      s = Asterism::Transfer::Sender.new("#{to}/transfer/inbox", "app", tar, net: net, name: name, meta: meta)
      s.logger = nil
      out.puts "fmrb_send: the app #{name} (#{n} file(s), #{s.size} bytes packed) to #{to}"
      until s.done?
        net.poll
        s.step
        sleep 0.0005
      end
      unless s.ok?
        out.puts "fmrb_send: not installed: #{s.reason}"
        return false
      end
      answer = s.answer
      said = false
      deadline = Time.now + answer_s
      while answer["status"] == "pending"
        unless said
          out.puts "fmrb_send: #{to} looks at it (its owner may be asked; up to #{answer_s} s)"
          said = true
        end
        if Time.now > deadline
          out.puts "fmrb_send: no answer from #{to} in #{answer_s} s"
          return false
        end
        sleep STATUS_EVERY_S
        answer = net["#{to}/transfer/inbox", timeout_ms: 10_000].asterism_call("status", s.id)
      end
      if answer["status"] == "installed"
        out.puts "fmrb_send: installed on #{to} as #{answer['path']} (#{answer['lang']})"
        true
      else
        out.puts "fmrb_send: not installed: #{answer['reason'] || answer['status']}"
        false
      end
    end
  end

  # One file to <to>/transfer/inbox over net (an Asterism::Connection).
  # -> true when placed.
  def self.send_file(net, to, path, overwrite: false, name: nil, sd: false, out: $stdout)
    meta = overwrite ? { "overwrite" => true } : {}
    meta["inbox"] = "sd" if sd
    s = Asterism::Transfer::Sender.new("#{to}/transfer/inbox", "file", path, net: net,
                                                                              name: name || File.basename(path),
                                                                              meta: meta)
    s.logger = nil
    out.puts "fmrb_send: #{path} (#{s.size} bytes) to #{to}"
    shown = 0
    stop = false
    old = trap("INT") { stop = true }
    begin
      until s.done?
        if stop
          s.cancel
          net.poll
          break
        end
        net.poll
        s.step
        q = s.size.zero? ? 4 : s.pos * 4 / s.size
        if q > shown
          shown = q
          out.puts format("fmrb_send: %d of %d bytes", s.pos, s.size) if q < 4
        end
        sleep 0.0005
      end
    ensure
      trap("INT", old || "DEFAULT")
    end
    st = s.stats
    note = "#{s.took_ms} ms, #{st['calls']} calls, pieces of #{s.piece}, #{s.window} at a time (the receiver's), " \
           "#{st['again']} again, #{st['retries']} retries"
    if s.ok? && s.answer["status"] == "placed"
      out.puts "fmrb_send: placed as #{s.answer['name']} in #{to}'s #{sd ? '/sd/inbox' : '/home/inbox'} (#{note})"
      true
    else
      why = s.ok? ? s.answer["reason"] : s.reason
      out.puts "fmrb_send: not sent: #{why} (#{note})"
      false
    end
  end
end

exit(FmrbSend.main(ARGV) ? 0 : 1) if $PROGRAM_NAME == __FILE__

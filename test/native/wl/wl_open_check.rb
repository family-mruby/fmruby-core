#!/usr/bin/env ruby
# frozen_string_literal: true
#
# wl_open_check.rb - files opened in Linux apps from the File Manager
# (doc/linux_native/, L11), in the Ubuntu 26.04 image with the program built
# by rake native:wl, headless (WLR_BACKENDS=headless, a 1280x800 output: a
# 640x400 screen at 2x), --machine linux_pc_wl, language = "ja".
#
#   ruby test/native/wl/wl_open_check.rb [--engine standard|mruby]
#                                        [--renderer pixman|auto] [--keep]
#
# HOME (so /mnt/host), XDG_DATA_HOME and XDG_CONFIG_HOME are directories of
# the run (a host directory mounted at /run/fmrb): the check's own .desktop
# entries (a script that writes down what it was given), its own globs
# (mime/globs2) for two made-up kinds, a mimeapps.list with the defaults,
# the user's associations.toml, and the files to open. Nothing is written
# outside the run directory and the container.
#
#   1. a kind the table does not list (doc.pdf), Enter in the File Manager:
#      Linux's default for it (mimeapps.list) is started and gets the file's
#      real path (/run/fmrb/home/doc.pdf for /mnt/host/doc.pdf)
#   2. the right click: "Linux のアプリで開く" in the menu, then the apps for
#      the kind, the default first (a NoDisplay=true handler among them);
#      the second one clicked opens the file
#   3. many apps (8 for a kind): the L key, 6 and "その他..." (Other), Down
#      to it and Enter, the next page, Enter on its first: the 7th app
#   4. the table: "linux:<id>" opens that app, "linux" the default
#   5. what stays as it was: note.txt (listed "edit") opens the editor,
#      hello.rb (listed "run") runs, and a kind neither the table nor Linux
#      knows (odd.fmnone) opens the editor
#
# Shots, logs and a summary go to tmp/wl_check/open-<engine>-<renderer>/.

require "digest"
require "fileutils"
require "json"
require "open3"
require "optparse"
require "socket"
require_relative "../../../tools/regress/screens/image"

CORE = File.expand_path("../../..", __dir__)
TOOLS = File.expand_path("../tools", CORE)
DEBUG_PORT = 15_566
MENU_BAR_H = 13
MENU_ITEM_H = 12
# Linux key codes (input-event-codes.h).
KEY_ENTER = 28
KEY_ESC = 1
KEY_L = 38
KEY_DOWN = 108
# file_manager.rb's layout.
FMGR_W = 300
FMGR_H = 206
FMGR_ITEM_H = 12
CTX_ITEM_H = 14
LX_PAGE = 6
MANY = 8

opts = { engine: "standard", renderer: "pixman", keep: false }
OptionParser.new do |o|
  o.on("--engine E") { |v| opts[:engine] = v }
  o.on("--renderer R") { |v| opts[:renderer] = v }
  o.on("--keep") { opts[:keep] = true }
end.parse!
abort "--engine standard|mruby" unless %w[standard mruby].include?(opts[:engine])
ENGINE = opts[:engine]
BIN = "native/build-wl/#{ENGINE == 'mruby' ? 'cmake-mruby' : 'cmake'}/fmruby-native"

def image_name
  file = File.join(CORE, "native/docker/Dockerfile.ubuntu")
  from = File.read(file)[/^FROM\s+ubuntu:(\S+)/, 1]
  "fmrb-native-build:#{from}-#{Digest::SHA256.file(file).hexdigest[0, 8]}"
end

IMAGE = image_name
abort "#{BIN} not found; rake native:wl" unless File.exist?(File.join(CORE, BIN))
abort "image #{IMAGE} not found; rake native:wl builds it" unless system("docker image inspect #{IMAGE} > /dev/null 2>&1")

def now = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def sh!(*cmd, ok: true)
  out, st = Open3.capture2e(*cmd)
  raise "#{cmd.join(' ')}:\n#{out.lines.last(15).join}" if ok && !st.success?
  [out, st]
end

class OpenRun
  attr_reader :results

  def initialize(renderer, keep)
    @renderer = renderer
    @keep = keep
    @name = "fmrb-wl-open-#{ENGINE}-#{renderer}"
    @run = "/tmp/fmrb-wl-open-#{ENGINE}-#{renderer}-#{Process.uid}"
    @out = File.join(CORE, "tmp/wl_check", "open-#{ENGINE}-#{renderer}")
    @results = []
  end

  def result(name, pass, detail)
    @results << { name: "open #{ENGINE}/#{@renderer} #{name}", pass: pass, detail: detail }
    puts "#{pass ? 'ok  ' : 'FAIL'} open #{ENGINE}/#{@renderer} #{name}: #{detail}"
  end

  # ------------------------------------------------------------ the run

  def write(rel, text, mode = 0o644)
    path = File.join(@run, rel)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, text)
    File.chmod(mode, path)
  end

  def desktop(id, text)
    write("data/applications/#{id}", "[Desktop Entry]\nType=Application\n#{text}")
  end

  def prepare
    FileUtils.rm_rf(@run)
    conf = File.read(File.join(CORE, "native/build/root/etc/system_conf.toml"))
    write("data/family-mruby-dev/etc/system_conf.toml", conf.sub(/^language = "en"/, 'language = "ja"'))
    # The user's table (/home/associations.toml): one kind to a named Linux
    # app, one to Linux's default.
    write("data/family-mruby-dev/home/associations.toml",
          "fmzz = \"linux:fmrb-zz\"\nfmdef = \"linux\"\n")
    # The apps: one script, which writes down its tag and the file.
    write("take.sh", "#!/bin/sh\necho \"$1|$2\" >> /run/fmrb/taken\n", 0o755)
    desktop("fmrb-pdf.desktop", "Name=PDF Taker\nExec=/run/fmrb/take.sh pdf %f\nMimeType=application/pdf;\n")
    desktop("fmrb-pdf2.desktop",
            "Name=Other Taker\nExec=/run/fmrb/take.sh other %U\nMimeType=application/pdf;\nNoDisplay=true\n")
    desktop("fmrb-zz.desktop", "Name=ZZ Taker\nExec=/run/fmrb/take.sh zz\nMimeType=application/x-fmrb-zz;\n")
    MANY.times do |i|
      desktop("fmrb-many-#{i + 1}.desktop",
              "Name=Many #{i + 1}\nExec=/run/fmrb/take.sh many#{i + 1} %f\nMimeType=application/x-fmrb-many;\n")
    end
    write("data/mime/globs2", "50:application/x-fmrb-zz:*.fmzz\n50:application/x-fmrb-many:*.fmmany\n" \
                              "50:application/x-fmrb-many:*.fmdef\n50:application/pdf:*.pdf\n")
    write("config/mimeapps.list", "[Default Applications]\napplication/pdf=fmrb-pdf.desktop;\n" \
                                  "application/x-fmrb-many=fmrb-many-3.desktop;\n")
    # The files (HOME is /mnt/host).
    write("home/doc.pdf", "%PDF-1.4\n")
    write("home/many.fmmany", "many\n")
    write("home/data.fmzz", "zz\n")
    write("home/pick.fmdef", "def\n")
    write("home/note.txt", "note\n")
    write("home/odd.fmnone", "odd\n")
    write("home/hello.rb", "Log.info(\"wl_open_check: hello.rb ran\")\n")
    # Lists two directories the way the File Manager does, to find rows.
    write("home/lister.app.rb", <<~RUBY)
      class ListerApp < FmrbApp
        def names(d)
          out = []
          dir = Dir.open(d)
          while (ent = dir.read)
            out << ent unless ent == "." || ent == ".."
          end
          dir.close
          out.sort
        end

        def on_create
          Log.info("lister: root=" + names("/").join(","))
          Log.info("lister: host=" + names("/mnt/host").join(","))
        end

        def on_update
          1000
        end
      end
      ListerApp.new.start
    RUBY
  end

  def start
    sh!("docker", "rm", "-f", @name, ok: false)
    a = ["docker", "run", "--rm", "--init", "--name", @name, "-d", "-p", "127.0.0.1:#{DEBUG_PORT}:5565",
         "-u", "#{Process.uid}:#{Process.gid}", "-v", "#{CORE}:/src", "-w", "/src", "-v", "#{@run}:/run/fmrb",
         "-e", "XDG_RUNTIME_DIR=/run/fmrb/xdg", "-e", "HOME=/run/fmrb/home", "-e", "LANG=C.UTF-8",
         "-e", "XDG_DATA_HOME=/run/fmrb/data", "-e", "XDG_CONFIG_HOME=/run/fmrb/config",
         "-e", "FMRB_NATIVE_RUN_DIR=/run/fmrb",
         "-e", "WLR_BACKENDS=headless", "-e", "WLR_HEADLESS_OUTPUTS=1", "-e", "SDL_AUDIODRIVER=dummy"]
    a += ["-e", "WLR_RENDERER=#{@renderer}"] if @renderer != "auto"
    prog = "#{BIN} --machine linux_pc_wl --set display.mode=1280x800"
    inner = "mkdir -p -m 700 /run/fmrb/xdg && #{prog} > /run/fmrb/core.log 2>&1; " \
            "echo \"exit=$?\" >> /run/fmrb/core.log"
    sh!(*a, IMAGE, "bash", "-c", inner)
  end

  def ctl(request, timeout: 30)
    sock = UNIXSocket.new(File.join(@run, "ctl"))
    sock.write("#{request}\n")
    data = +""
    deadline = now + timeout
    loop do
      raise "control socket: no answer to #{request.inspect} in #{timeout}s" if now > deadline
      next unless IO.select([sock], nil, nil, 0.5)
      chunk = sock.read_nonblock(65_536, exception: false)
      break if chunk.nil?
      next if chunk == :wait_readable
      data << chunk
    end
    sock.close
    data.force_encoding("UTF-8")
  end

  def comp(*words)
    r = ctl("comp #{words.join(' ')}")
    raise "comp #{words.join(' ')}: #{r}" unless r.start_with?("OK")
    r
  end

  def info
    r = comp("info")
    r.split.drop(1).to_h { |kv| kv.split("=", 2) }
  end

  def frame_size = @info["frame"].split("x").map(&:to_i)

  def to_out(fx, fy)
    s = @info["scale"].to_i
    x0, y0 = @info["at"].split(",").map(&:to_i)
    [x0 + fx * s + s / 2, y0 + fy * s + s / 2]
  end

  def click(fx, fy) = comp("click", *to_out(fx, fy))

  def right_click(fx, fy)
    comp("move", *to_out(fx, fy))
    comp("button", 3, 1)
    comp("button", 3, 0)
  end

  def tap(code)
    comp("key", code, 1)
    comp("key", code, 0)
  end

  def shot(name)
    path = File.join(@out, name)
    sh!("ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, "shot", path)
    path
  end

  def native_tool(*args, timeout: 30)
    cmd = ["timeout", timeout.to_s, "ruby", File.join(TOOLS, "fmrb_native.rb"), "--run-dir", @run, *args.map(&:to_s)]
    sh!({ "FMRB_DEBUG_TCP_PORT" => DEBUG_PORT.to_s }, *cmd, ok: false)
  end

  def log_text = File.exist?(File.join(@run, "core.log")) ? File.read(File.join(@run, "core.log")).force_encoding("UTF-8").scrub : ""

  def taken = File.exist?(File.join(@run, "taken")) ? File.read(File.join(@run, "taken")).lines.map(&:strip) : []

  def wait_for(what, timeout)
    deadline = now + timeout
    loop do
      v = yield
      return v if v
      raise "#{what}: not within #{timeout}s" if now > deadline
      sleep 0.2
    end
  end

  # ------------------------------------------------- the File Manager

  # Its rectangle on the frame (system_desktop.app.rb: centred under the bar).
  def fmgr_xy
    w, h = frame_size
    [(w - FMGR_W) / 2, MENU_BAR_H + (h - MENU_BAR_H - FMGR_H) / 2 + 2]
  end

  def row_xy(i)
    x, y = fmgr_xy
    [x + 40, y + 14 + 1 + FMGR_ITEM_H + 1 + i * FMGR_ITEM_H + FMGR_ITEM_H / 2]
  end

  def open_fmgr
    click(30, 5)
    sleep 0.8
    click(30, MENU_BAR_H + 1 + 2 * MENU_ITEM_H + MENU_ITEM_H / 2)
    sleep 1.0
  end

  def enter_row(i)
    click(*row_xy(i))
    sleep 0.4
    tap(KEY_ENTER)
    sleep 0.8
  end

  # Open the File Manager and go to /mnt/host ("/" then "mnt" then "host").
  # It remembers the folder it was closed in for the rest of the run
  # (doc/app_state), and this check never leaves /mnt/host, so only the
  # first opening has to walk there.
  def to_host
    open_fmgr
    return if @fmgr_at_host
    enter_row(@root.index("mnt"))
    enter_row(1)
    @fmgr_at_host = true
  end

  # The row of a file in /mnt/host ("..", then the names).
  def host_row(name) = 1 + @host.index(name)

  def taken_after(n, what)
    wait_for(what, 15) { taken.size > n && taken[n..] }
  end

  # ------------------------------------------------------------ checks

  def go
    FileUtils.rm_rf(@out)
    FileUtils.mkdir_p(@out)
    prepare
    start
    wait_for("Boot complete", 120) { log_text.include?("Boot complete") || log_text.include?("refused") }
    raise "refused: #{log_text[/native: refused.*/]}" if log_text.include?("native: refused")
    wait_for("the desktop", 60) { log_text.include?("Linux windows are Family mruby windows") }
    wait_for("the Linux apps read", 30) { log_text =~ /linux apps: \d+ entries of/ }
    @info = info
    sleep 1.5

    out, = native_tool("app", "spawn", "/mnt/host/lister.app.rb")
    lpid = out[/"pid":\s*(\d+)/, 1]
    wait_for("the lister", 20) { log_text.include?("lister: host=") }
    @root = log_text[/lister: root=([^\n]*)/, 1].split(",")
    @host = log_text[/lister: host=([^\n]*)/, 1].split(",")
    native_tool("app", "kill", lpid) if lpid
    sleep 1.0
    raise "no mnt at / (#{@root.join(',')})" unless @root.include?("mnt")

    # 1. a kind the table does not list: Linux's default
    to_host
    shot("host.png")
    n = taken.size
    enter_row(host_row("doc.pdf"))
    got = taken_after(n, "doc.pdf taken") rescue []
    line = log_text[/File manager: open \/mnt\/host\/doc\.pdf in the Linux app [^\n]*/]
    result("not in the table: Linux's default", got == ["pdf|/run/fmrb/home/doc.pdf"] && !line.nil?,
           "Enter on /mnt/host/doc.pdf: #{line || 'no open line'}; the app got #{got.inspect} " \
           "(#{log_text[/open \/mnt\/host\/doc\.pdf \([^)]*\) in "[^"]*"[^\n]*/]})")

    # 2. the right click, the menu, the second app by the mouse
    to_host
    rx, ry = row_xy(host_row("doc.pdf"))
    right_click(rx, ry)
    sleep 0.8
    shot("context_menu.png")
    # The menu's items for a file that does not run: Edit, Copy, Delete,
    # then the Linux item (its top left at the click, inside the window).
    fx, fy = fmgr_xy
    ctx_h = CTX_ITEM_H * 4 + 2
    cy = ry + ctx_h > fy + FMGR_H ? fy + FMGR_H - ctx_h : ry
    click(rx + 10, cy + 1 + 3 * CTX_ITEM_H + CTX_ITEM_H / 2)
    sleep 0.8
    shot("linux_menu.png")
    apps_line = log_text.scan(/File manager: Linux apps for \/mnt\/host\/doc\.pdf \(([^)]*)\): (\d+)/).last
    # The apps' menu: where the context menu was, 2 apps.
    lx_h = CTX_ITEM_H * 2 + 2
    ly = cy + lx_h > fy + FMGR_H - 2 ? fy + FMGR_H - lx_h - 2 : cy
    n = taken.size
    click(rx + 10, ly + 1 + CTX_ITEM_H + CTX_ITEM_H / 2)
    got = taken_after(n, "the second app") rescue []
    result("the right click", apps_line == ["application/pdf", "2"] && got == ["other|/run/fmrb/home/doc.pdf"],
           "the menu's Linux item: apps for #{apps_line&.first} #{apps_line&.last} (the default PDF Taker, then " \
           "the NoDisplay=true Other Taker); the second clicked got #{got.inspect}")

    # 3. many apps: the L key, Other, the next page
    to_host
    click(*row_xy(host_row("many.fmmany")))
    sleep 0.4
    tap(KEY_L)
    sleep 0.8
    shot("many_page1.png")
    LX_PAGE.times { tap(KEY_DOWN) }
    tap(KEY_ENTER)
    sleep 0.8
    shot("many_page2.png")
    n = taken.size
    tap(KEY_ENTER)
    got = taken_after(n, "the 7th app") rescue []
    many = log_text[/File manager: Linux apps for \/mnt\/host\/many\.fmmany \(([^)]*)\): (\d+)/, 2]
    # In order: the default (Many 3), then by name: 1 2 4 5 6 | 7 8.
    result("many apps, by keys", many == MANY.to_s && got == ["many7|/run/fmrb/home/many.fmmany"],
           "#{many} apps for many.fmmany; L, Down x#{LX_PAGE} to Other, Enter, Enter: #{got.inspect} " \
           "(page 1: Many 3 (default), 1, 2, 4, 5, 6, Other; page 2: 7, 8)")

    # 4. the table: a named Linux app, and Linux's default
    to_host
    n = taken.size
    enter_row(host_row("data.fmzz"))
    zz = taken_after(n, "data.fmzz taken") rescue []
    to_host
    n = taken.size
    enter_row(host_row("pick.fmdef"))
    df = taken_after(n, "pick.fmdef taken") rescue []
    result("the table's Linux answers", zz == ["zz|/run/fmrb/home/data.fmzz"] && df == ["many3|/run/fmrb/home/pick.fmdef"],
           "fmzz = \"linux:fmrb-zz\": #{zz.inspect} (Exec without a field code: the path last); " \
           "fmdef = \"linux\": #{df.inspect} (the default for its kind)")

    # 5. what stays as it was
    to_host
    enter_row(host_row("hello.rb"))
    ran = wait_for("hello.rb ran", 15) { log_text.include?("wl_open_check: hello.rb ran") } rescue false
    to_host
    enter_row(host_row("note.txt"))
    edit_txt = wait_for("note.txt in the editor", 15) { log_text.include?("File manager: edit /mnt/host/note.txt") } rescue false
    sleep 1.5
    shot("editor_note.png")
    ps, = native_tool("app", "ps")
    ps.scan(/"pid":\s*(\d+)[^}]*"name":\s*"editor/).flatten.each { |pid| native_tool("app", "kill", pid) }
    sleep 1.0
    to_host
    enter_row(host_row("odd.fmnone"))
    edit_odd = wait_for("odd.fmnone in the editor", 15) { log_text.include?("File manager: edit /mnt/host/odd.fmnone") } rescue false
    none = log_text.include?("no Linux app opened /mnt/host/odd.fmnone")
    result("as before", ran && edit_txt && edit_odd && none && taken.none? { |l| l.include?("note.txt") || l.include?("hello.rb") },
           "hello.rb (\"run\") ran: #{ran}; note.txt (\"edit\") in the editor: #{edit_txt}; odd.fmnone " \
           "(no kind known, no Linux app) in the editor: #{edit_odd} (asked Linux first: #{none})")
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log"))
  rescue StandardError => e
    result("run", false, e.message.lines.first(3).join.strip)
    FileUtils.cp(File.join(@run, "core.log"), File.join(@out, "core.log")) if File.exist?(File.join(@run, "core.log"))
  ensure
    sh!("docker", "rm", "-f", @name, ok: false) unless @keep
    FileUtils.rm_rf(@run) unless @keep
  end
end

run = OpenRun.new(opts[:renderer], opts[:keep])
run.go
out = File.join(CORE, "tmp/wl_check")
FileUtils.mkdir_p(out)
File.write(File.join(out, "summary-open-#{ENGINE}-#{opts[:renderer]}.json"), JSON.pretty_generate(run.results))
failed = run.results.reject { |r| r[:pass] }
puts "wl_open_check #{ENGINE} #{opts[:renderer]}: #{run.results.length - failed.length}/#{run.results.length} passed (tmp/wl_check/)"
exit(failed.empty? ? 0 : 1)

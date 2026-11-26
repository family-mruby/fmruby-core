#!/usr/bin/env ruby
# Host tests for the remote shell's machine side
# (flash/usr/share/transfer/rshell.rb, doc/remote_shell R1): reading a line
# (blanks, "a b", a\ b), folding paths (".", "..", "//"), the secrets kept
# out (cat, ls, cd, stat, read and the completion; /flash/... and ".." too),
# the output in pieces (OUT_MAX a call, cut where a UTF-8 character starts,
# TOTAL_MAX in all, the same pos asked again), head and tail, the
# completion (command names, paths, folders with "/", names with blanks,
# at most COMPLETE_MAX) and the door (the switch, who is let in, one
# session, the idle time).
#
# rshell.rb runs here as it ships, in a module whose File, FmrbFile, FmrbApp,
# FmrbConst, Machine and Log are stand-ins over a temporary directory (the
# machine's "/" is that directory). FmrbFile.secret_path? is the rule of
# main/kernel/fmrb_secret_path.c written again in Ruby (the C itself is
# tested by rake secret:test). receive.toml is read by the real
# FmrbReceiveConf with its paths moved into the temporary directory.
#
#   ruby test/rshell/run.rb      (rake rshell:test, in rake test)

require "tmpdir"
require "fileutils"
require_relative "../toml/host_toml"

$failed = 0
$count = 0

def check(what, got, want)
  $count += 1
  return if got == want

  $failed += 1
  puts "  FAIL #{what}: got #{got.inspect[0, 300]}, want #{want.inspect[0, 300]}"
end

def raises(what, pattern)
  yield
  $count += 1
  $failed += 1
  puts "  FAIL #{what}: no error"
rescue ArgumentError => e
  check(what, e.message.include?(pattern), true)
  puts "    (message: #{e.message})" unless e.message.include?(pattern)
end

ROOT = File.expand_path("../..", __dir__)
TMP = Dir.mktmpdir("fmrb_rshell_test")
at_exit { FileUtils.rm_rf(TMP) }
FS = File.join(TMP, "fs")

src = File.read("#{ROOT}/flash/usr/share/transfer/receive_conf.rb")
          .gsub("/home/asterism", "#{FS}/home/asterism").gsub("/etc/asterism", "#{FS}/etc/asterism")
eval(src, TOPLEVEL_BINDING, "receive_conf.rb")

module Log
  @lines = []
  def self.lines = @lines
  def self.info(s) = @lines << s
  def self.warn(s) = @lines << s
end

# The machine, over FS.
module Box
  def self.real(p) = FS + (p == "/" ? "" : p)

  OPENS = [0]

  class File
    def self.directory?(p) = ::File.directory?(Box.real(p))
    def self.exist?(p) = ::File.exist?(Box.real(p))
    def self.file?(p) = ::File.file?(Box.real(p))

    def self.open(p, mode = "r")
      Box::OPENS[0] += 1
      ::File.open(Box.real(p), mode == "r" ? "rb" : mode)
    end
  end

  module FmrbFile
    def self.list(d)
      r = Box.real(d)
      return nil unless ::File.directory?(r)
      Dir.children(r).map { |n| ::File.directory?(::File.join(r, n)) ? "#{n}/" : n }
    end

    def self.size(p) = (::File.file?(Box.real(p)) ? ::File.size(Box.real(p)) : nil)
    def self.path_read_only?(_p) = false
    def self.free_bytes(p) = (p == "/tmp" ? 100_000 : 2_000_000)

    # fmrb_secret_path.c: fmrb_path_classify(path) != FMRB_PATH_PLAIN
    def self.secret_path?(p)
      segs = []
      dotdot = false
      first = true
      p.split("/").each do |s|
        next if s.empty? || s == "."
        if s == ".."
          dotdot = true
          next
        end
        if first && s == "flash"
          first = false
          next
        end
        first = false
        segs << s
      end
      return true if dotdot
      return true if segs[0] == "home" && segs[1] == "asterism"
      segs.size == 2 && %w[home etc].include?(segs[0]) &&
        (segs[1].start_with?("wifi.toml") || segs[1].start_with?("secrets.toml"))
    end
  end

  module FmrbApp
    def self.ps = [{ id: 0, type: 0, state: 2, vm_type: 3, name: "kernel" },
                   { id: 4, type: 2, state: 2, vm_type: 0, name: "Transfer" }]
    def self.pid = 4
    def self.heap_info = { free: 3_000_000, total: 8_000_000, min_free: 2_000_000, largest_block: 1_000_000,
                           iram_free: 100_000, iram_total: 300_000 }
    def self.sys_pool_info = { total: 1_048_576, used: 524_288, free: 524_288, used_blocks: 3, free_blocks: 1 }
    def self.pool_used = 200_000
    def self.pool_usage = 10
  end

  module FmrbConst
    OS_VERSION = "9.9.9"
    GA_VERSION = "9.9.9"
    LINK_VERSION = 3
    BOARD = "linux"
    HW_FAMILY = "modern"
    CHIP_MODEL = "linux"
    BUILD_DATE = "today"
    IDF_VERSION = "-"
  end

  module Machine
    @now = 1000
    def self.board_millis = @now
    def self.now=(v)
      @now = v
    end
  end
end
Box.module_eval(File.read("#{ROOT}/flash/usr/share/transfer/rshell.rb"), "rshell.rb")
R = Box::RShell

def put(path, text)
  r = Box.real(path)
  FileUtils.mkdir_p(File.dirname(r))
  File.binwrite(r, text)
end

FileUtils.mkdir_p(Box.real("/tmp"))
FileUtils.mkdir_p(Box.real("/home/docs"))
put("/home/memo.txt", "one\ntwo\nthree\n")
put("/home/my file.txt", "spaced\n")
put("/home/wifi.toml", "ssid = \"x\"\npassword = \"y\"\n")
put("/etc/secrets.toml", "key = \"z\"\n")
put("/home/asterism/config.toml", "node = \"n\"\n")
put("/home/asterism/node.tar", "KEY")
put("/home/docs/a.txt", "a\n")
put("/home/docs/b.txt", "b\n")
lines = (1..30).map { |i| "line #{i}\n" }.join
put("/home/lines.txt", lines)
put("/home/big.txt", "0123456789abcdef" * 1280)            # 20 KB
put("/home/huge.txt", ("x" * 99 + "\n") * 1000)             # 100 KB
put("/home/kana.txt", "あ" * 10_000)                    # 30000 bytes, 3 each
put("/home/bin.dat", "ab\0cd")
FileUtils.mkdir_p(Box.real("/home/many"))
300.times { |i| File.write(Box.real(format("/home/many/f%03d", i)), "") }

S = R::Session.new(1, "pc", 0)
def run(line, write = false) = S.exec(line, write)

puts "reading a line"
check("blanks", R.words("  ls\t-l   /home "), ["ls", "-l", "/home"])
check("quotes", R.words("cat \"my file.txt\""), ["cat", "my file.txt"])
check("backslash", R.words("cat my\\ file.txt"), ["cat", "my file.txt"])
check("quote inside a word", R.words("cat a\"b c\"d"), ["cat", "ab cd"])
check("an escaped quote", R.words("cat a\\\"b"), ["cat", "a\"b"])
check("an open quote", R.split_line("cat \"my fi")[1], true)
check("starts", R.split_line("ls  /a")[0].map { |w| w[1] }, [0, 4])
check("escape", R.escape("my file \"x\"\\"), "my\\ file\\ \\\"x\\\"\\\\")

puts "paths"
check("relative", R.resolve("/home", "docs"), "/home/docs")
check("..", R.resolve("/home", ".."), "/")
check("past the top", R.resolve("/home", "../../../etc"), "/etc")
check("// . ..", R.resolve("/home", "a//b/./c/../d/"), "/home/a/b/d")
check("absolute", R.resolve("/home", "/tmp/./x"), "/tmp/x")
check("empty", R.resolve("/home/docs", ""), "/home/docs")

puts "the secrets"
check("cat config.toml", run("cat /home/asterism/config.toml")["code"], 1)
check("cat says why", run("cat /home/asterism/config.toml")["out"].include?("kept from the remote shell"), true)
check("cat wifi.toml", run("cat wifi.toml")["code"], 1)
check("cat by .. from /tmp", run("cat ../home/asterism/node.tar")["code"], 1)
check("cat /flash/...", run("cat /flash/home/asterism/node.tar")["code"], 1)
check("cat /etc/secrets.toml", run("cat /etc/secrets.toml")["code"], 1)
check("head", run("head /home/wifi.toml")["code"], 1)
check("tail", run("tail -n 1 /home/wifi.toml")["code"], 1)
check("stat", run("stat /home/wifi.toml")["code"], 1)
check("cd", run("cd /home/asterism")["code"], 1)
check("cd stays", S.cwd, "/home")
check("ls the folder", run("ls /home/asterism")["out"].include?("kept from"), true)
check("ls a name in it: not whether it is there",
      [run("ls /home/asterism/node.tar")["out"].include?("kept from"),
       run("ls /home/asterism/nothing")["out"].include?("kept from")], [true, true])
check("ls /home names it", run("ls /home")["out"].split("\n").include?("asterism/"), true)
check("ls /home names wifi.toml", run("ls")["out"].split("\n").include?("wifi.toml"), true)
check("ls -l: no size for a secret", run("ls -l /home")["out"].split("\n").grep(/wifi\.toml/).first.strip, "-  wifi.toml")
check("ls -l: a size", run("ls -l /home")["out"].split("\n").grep(/memo/).first.strip, "14  memo.txt")
raises("read refuses", "kept from") { S.read("/home/asterism/node.tar", 0, 10) }
raises("read refuses by ..", "kept from") { S.read("/tmp/../home/wifi.toml", 0, 10) }
check("complete in the folder", S.complete("cat /home/asterism/", nil)["items"], [])
check("complete names the folder", S.complete("cat /home/ast", nil)["items"], ["/home/asterism/"])

puts "the commands"
check("pwd", run("pwd")["out"], "/home\n")
check("cd docs", [run("cd docs")["code"], S.cwd], [0, "/home/docs"])
check("ls here", run("ls")["out"], "a.txt\nb.txt\n")
check("cd ..", [run("cd ..")["code"], S.cwd], [0, "/home"])
check("cd nowhere", run("cd nowhere")["code"], 1)
check("cd: back home", [run("cd /tmp")["cwd"], run("cd")["cwd"]], ["/tmp", "/home"])
check("cat a name with a blank", run("cat my\\ file.txt")["out"], "spaced\n")
check("cat in quotes", run("cat \"my file.txt\"")["out"], "spaced\n")
check("cat not text", run("cat bin.dat")["code"], 1)
check("cat a directory", run("cat docs")["code"], 1)
check("cat two", run("cat a b")["code"], 2)
check("head", run("head -n 2 lines.txt")["out"], "line 1\nline 2\n")
check("head default", run("head lines.txt")["out"].count("\n"), 10)
check("tail", run("tail -n 3 lines.txt")["out"], "line 28\nline 29\nline 30\n")
check("tail more than there", run("tail -n 99 memo.txt")["out"], "one\ntwo\nthree\n")
check("head -n x", run("head -n x memo.txt")["code"], 2)
check("stat", run("stat memo.txt")["out"], "/home/memo.txt\n  type: file\n  size: 14 bytes\n  read-only: no\n")
check("stat a dir", run("stat docs")["out"].include?("type: directory"), true)
check("df", run("df")["out"].include?("/tmp"), true)
check("free", run("free")["out"].include?("system pool: 512 KB used of 1024 KB"), true)
check("ps", run("ps")["out"].include?("  4  user    running   mruby  Transfer (this)"), true)
check("ver", run("ver")["out"].start_with?("Family mruby 9.9.9"), true)
check("help names rm", run("help")["out"].include?("rm <path...>"), true)
check("an empty line", run("   ")["out"], "")
check("no such command", run("frob")["code"], 127)
check("a change by exec_read", run("rm memo.txt")["code"], 126)
check("a change by exec_write", [run("rm memo.txt", true)["code"], run("rm memo.txt", true)["out"]],
      [127, "rm: not in this version of the remote shell\n"])
check("still there", File.exist?(Box.real("/home/memo.txt")), true)
check("the table", [R.commands["ls"], R.commands["mv"], R.commands.size], ["read", "write", 18])

puts "the output in pieces"
def whole(first, read_more)
  out = first["out"].dup
  pages = [first["out"]]
  pos = first["more"]
  while pos
    a = read_more.call(pos)
    out << a["out"]
    pages << a["out"]
    pos = a["more"]
    last = a
  end
  [out, pages, last || first]
end
a = run("cat big.txt")
check("big: the first piece", [a["out"].bytesize, a["more"]], [8192, 8192])
w = whole(a, ->(p) { S.more(p) })
check("big: all of it", w[0], File.binread(Box.real("/home/big.txt")))
check("big: not cut", w[2]["cut"], false)
again = S.more(8192)
check("the same pos again", [again["out"].bytesize, again["more"]], [8192, 16384])
opens = Box::OPENS[0]
S.more(16384)
check("a read that follows on keeps the File", Box::OPENS[0], opens)
a = run("cat huge.txt")
w = whole(a, ->(p) { S.more(p) })
check("huge: cut at 64 KB", [w[0].start_with?(File.binread(Box.real("/home/huge.txt"))[0, 65536]), w[2]["cut"]], [true, true])
check("huge: says so", w[0].end_with?("(the output is cut at 64 KB)\n"), true)
check("huge: every piece fits", w[1].map(&:bytesize).max <= 8192 + 40, true)
a = run("cat kana.txt")
w = whole(a, ->(p) { S.more(p) })
check("kana: every piece whole characters", w[1].all? { |s| s.dup.force_encoding("UTF-8").valid_encoding? }, true)
check("kana: all of it", w[0], File.binread(Box.real("/home/kana.txt")))
check("kana: pieces under the limit", w[1].map(&:bytesize).max <= 8192, true)
put("/home/many_kana.txt", ("あ" * 30 + "\n") * 2000)    # 182 KB
a = run("cat many_kana.txt")
w = whole(a, ->(p) { S.more(p) })
check("kana past 64 KB: whole characters", w[1].all? { |s| s.dup.force_encoding("UTF-8").valid_encoding? }, true)
check("kana past 64 KB: cut", [w[2]["cut"], w[0].bytesize <= 65536 + 40], [true, true])
a = run("ls many")
w = whole(a, ->(p) { S.more(p) })
check("a long ls in pieces", w[0].split("\n").size, 300)
check("text: a pos past the end", S.more(999_999), { "out" => "", "more" => nil, "cut" => false })
check("the next command forgets the last", [run("pwd")["more"], S.more(8192)["out"]], [nil, ""])
check("utf8_cut", [R.utf8_cut("aあb", 2), R.utf8_cut("aあb", 4), R.utf8_cut("ab", 1)], [1, 4, 1])
check("utf8_whole", [R.utf8_whole("aあ".byteslice(0, 3)), R.utf8_whole("aあ"), R.utf8_whole("ab")], [1, 4, 2])

puts "read"
r = S.read("big.txt", 100, 16)
check("read a part", [r["data"], r["pos"], r["size"]], ["456789abcdef0123", 100, 20480])
check("read at the end", S.read("big.txt", 20480, 100)["data"], "")
raises("read too much", "len: 1 to 8192") { S.read("big.txt", 0, 8193) }
raises("read a directory", "not a file") { S.read("docs", 0, 1) }

puts "the completion"
check("all commands", S.complete("", 0)["items"].size, 18)
check("c...", S.complete("c", nil), { "word" => 0, "items" => ["cat", "cd", "cp"], "cut" => false })
check("a path", S.complete("cat me", nil), { "word" => 4, "items" => ["memo.txt"], "cut" => false })
check("a folder ends in /", S.complete("cd do", nil)["items"], ["docs/"])
check("in a folder", S.complete("cat docs/", nil)["items"], ["docs/a.txt", "docs/b.txt"])
check("absolute", S.complete("ls /ho", nil)["items"], ["/home/"])
check("a blank in a name", S.complete("cat my", nil)["items"], ["my\\ file.txt"])
check("typed with a backslash", S.complete("cat my\\ f", nil), { "word" => 4, "items" => ["my\\ file.txt"], "cut" => false })
check("typed in a quote", S.complete("cat \"my f", nil)["items"], ["my\\ file.txt"])
check("after a blank: everything here", S.complete("cat ", nil)["items"].include?("memo.txt"), true)
check("pos in the middle", S.complete("cat me xyz", 6)["items"], ["memo.txt"])
check("a second word too", S.complete("ls -l me", nil)["items"], ["memo.txt"])
c = S.complete("ls many/", nil)
check("at most 200", [c["items"].size, c["cut"]], [200, true])
check("nothing there", S.complete("cat nothing/x", nil)["items"], [])
check("word counts characters", S.complete("cat あ me", nil)["word"], 6)

puts "the door"
HOME_CONF = Box.real("/home/asterism/receive.toml")
def conf(text) = File.write(HOME_CONF, text)
D = R::Door.new(nil)
conf("enabled = true\n")
raises("off", "lets no remote shell in (rshell_enabled is off") { D.open(from: "pc") }
conf("rshell_enabled = true\n")
raises("on, nobody listed", "from pc") { D.open(from: "pc") }
conf("rshell_enabled = true\n[rshell]\nfrom = [\"pc\"]\n")
o = D.open(from: "pc")
check("open", [o["id"], o["cwd"], o["commands"]["cat"], o["version"]["rshell"], o["out_max"]], [1, "/home", "read", 1, 8192])
check("exec_read", D.exec_read(1, "pwd", from: "pc")["out"], "/home\n")
check("exec_write runs a look", D.exec_write(1, "pwd", from: "pc")["out"], "/home\n")
raises("a node not let in", "lets no remote shell in from other") { D.exec_read(1, "pwd", from: "other") }
raises("a wrong id", "no remote shell session 7") { D.exec_read(7, "pwd", from: "pc") }
conf("rshell_enabled = true\n[rshell]\nfrom = [\"pc\", \"pc2\"]\n")
raises("one session at a time", "has a remote shell open from pc") { D.open(from: "pc2") }
Box::Machine.now = 1000 + R::IDLE_MS
o2 = D.open(from: "pc2")
check("an idle one is replaced", [o2["id"], D.s.from], [2, "pc2"])
check("the same node opens again", D.open(from: "pc2")["id"], 3)
Box::Machine.now = 1000 + 2 * R::IDLE_MS + 1
D.tick(Box::Machine.board_millis)
check("idle closes", D.s, nil)
raises("closed", "no remote shell session 3") { D.more(3, 0, from: "pc2") }
conf("rshell_enabled = true\n\n" \
     "# ---- managed by the console (below this line; edit above it) ----\n" \
     "[managed]\nby = \"console\"\nat = \"2026-11-26T00:00:00Z\"\n\n[managed.rshell]\nfrom = [\"console-*\"]\n")
check("from the console's list", D.open(from: "console-a")["id"], 4)
check("complete through the door", D.complete(4, "c", 1, from: "console-a")["items"], ["cat", "cd", "cp"])
check("read through the door", D.read(4, "memo.txt", 0, 3, from: "console-a")["data"], "one")
check("close", [D.close(4, from: "console-a"), D.s], [true, nil])
D.open(from: "console-a")
conf("enabled = true\n")
raises("turned off: refused", "rshell_enabled is off") { D.exec_read(5, "pwd", from: "console-a") }
check("turned off: the session ends", D.s, nil)

check("METHODS: the door's arguments",
      R::METHODS.map { |m, h| [m, h[:args].size] },
      R::METHODS.keys.map { |m| [m, R::Door.instance_method(m).parameters.count { |t, _| t == :req }] })
check("METHODS: every door takes from:", R::METHODS.keys.all? { |m| R::Door.instance_method(m).parameters.include?([:key, :from]) }, true)

FakeConn = Struct.new(:exposed) do
  def expose(name, obj, **kw) = (exposed[name] = [obj, kw]; "x")
  def unexpose(name) = !exposed.delete(name).nil?
end
FakeLink = Struct.new(:conn, :up) do
  def connected? = up
  def node = "sim"
end
conf("rshell_enabled = true\n[rshell]\nfrom = [\"pc\"]\n")
link = FakeLink.new(FakeConn.new({}), false)
D.switch(true, link)
check("switch on, not connected: nothing exposed", link.conn.exposed.keys, [])
link.up = true
D.switch(true, link)
check("switch on: exposed with caller: :from", [link.conn.exposed.keys, link.conn.exposed["rshell"][1][:caller]], [["rshell"], :from])
D.open(from: "pc")
D.switch(false, link)
check("switch off: taken away, the session ends", [link.conn.exposed.keys, D.s], [[], nil])

# The app loader takes a script of 64 KB at most (main/app/fmrb_app.c,
# "Script file too large"): the transfer app was 57 bytes under it after
# the look commands, and its receiving side went to transfer_inbox.rb to
# keep at least 8 KB free (R2).
check("transfer.app.rb 8 KB under the loader's 64 KB",
      File.size("#{ROOT}/flash/usr/share/transfer/transfer.app.rb") <= 65536 - 8192, true)

puts($failed.zero? ? "rshell: all #{$count} ok" : "rshell: #{$failed} of #{$count} FAILED")
exit($failed.zero? ? 0 : 1)

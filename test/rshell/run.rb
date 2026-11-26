#!/usr/bin/env ruby
# Host tests for the remote shell's machine side
# (flash/usr/share/transfer/rshell.rb, doc/remote_shell R1): reading a line
# (blanks, "a b", a\ b), folding paths (".", "..", "//"), the secrets kept
# out (cat, ls, cd, stat, read and the completion; /flash/... and ".." too),
# the output in pieces (OUT_MAX a call, cut where a UTF-8 character starts,
# TOTAL_MAX in all, the same pos asked again), head and tail, the
# completion (command names, paths, folders with "/", names with blanks,
# at most COMPLETE_MAX; kill's app names, open's apps) and the door (the
# switch, a checked connection, who is let in, one session, the idle time,
# the menu bar's mark). R2: the commands that change the machine (cp, mv,
# rm, mkdir with the secrets kept out as source and destination, kill and
# open with the kernel's answers coming through more) and exec_read
# refusing them. R4: put's write_begin / write_chunk / write_end into a
# hidden piece file (pieces again, out of order, past the size, the
# refusals, cancel, close, another begin, the idle time), get's read of a
# binary file, and the long cp and mv a piece at a time (progress, stop).
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
      ::File.open(Box.real(p), mode == "r" ? "rb" : (mode == "w" ? "wb" : mode))
    end

    def self.delete(p) = ::File.delete(Box.real(p))

    # no rename across "/sd" and the rest (another storage): raises
    def self.rename(a, b)
      raise "cross-device" if a.start_with?("/sd/") != b.start_with?("/sd/")
      ::File.rename(Box.real(a), Box.real(b))
    end
  end

  class Dir
    def self.mkdir(p) = ::Dir.mkdir(Box.real(p))
    def self.rmdir(p) = ::Dir.rmdir(Box.real(p))
  end

  RO = []

  module FmrbFile
    def self.list(d)
      r = Box.real(d)
      return nil unless ::File.directory?(r)
      ::Dir.children(r).map { |n| ::File.directory?(::File.join(r, n)) ? "#{n}/" : n }
    end

    def self.size(p) = (::File.file?(Box.real(p)) ? ::File.size(Box.real(p)) : nil)
    def self.path_read_only?(p) = Box::RO.any? { |r| p == r || p.start_with?("#{r}/") }
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
                   { id: 2, type: 1, state: 2, vm_type: 0, name: "system_desktop" },
                   { id: 4, type: 2, state: 2, vm_type: 0, name: "Transfer" },
                   { id: 5, type: 2, state: 2, vm_type: 0, name: "Note Pad" },
                   { id: 6, type: 2, state: 2, vm_type: 0, name: "Clock" },
                   { id: 7, type: 2, state: 2, vm_type: 0, name: "Clock" },
                   { id: 8, type: 2, state: 0, vm_type: 0, name: "Gone" }]
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
    PROC_ID_KERNEL = 0
    MSG_TYPE_APP_CONTROL = 1
  end

  module Machine
    @now = 1000
    @auto = 0
    # auto: the clock goes on by this much on every reading (a long cp's
    # steps)
    def self.board_millis = (@now += @auto)
    def self.now=(v)
      @now = v
    end

    def self.auto=(v)
      @auto = v
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
check("help names rm", run("help")["out"].include?("rm [-r] <path...>"), true)
check("an empty line", run("   ")["out"], "")
check("no such command", run("frob")["code"], 127)
check("a change by exec_read", run("rm memo.txt")["code"], 126)
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
# 64 KB falls inside a character after a whole piece (the pieces of this
# text end at 65534, and its byte 65534 starts a 3-byte one): its first
# bytes are left out, not sent before the note as a broken character
put("/home/long_kana.txt", "\u3044\u308d\u306f\u306b\u307b\u3078\u3068 \u3061\u308a\u306c\u308b\u3092 \u308f\u304b\u3088\u305f\u308c\u305d \u3064\u306d\u306a\u3089\u3080\n" * 1500)
a = run("cat long_kana.txt")
w = whole(a, ->(p) { S.more(p) })
check("kana cut inside a character: whole characters", w[1].all? { |s| s.dup.force_encoding("UTF-8").valid_encoding? }, true)
check("kana cut inside a character: the note right after the last whole one",
      w[0].dup.force_encoding("UTF-8").then { _1.valid_encoding? && _1.bytesize == 65534 + 30 && _1.end_with?("\u308c\n(the output is cut at 64 KB)\n") }, true)
a = run("ls many")
w = whole(a, ->(p) { S.more(p) })
check("a long ls in pieces", w[0].split("\n").size, 300)
check("text: a pos past the end", S.more(999_999), { "out" => "", "more" => nil, "cut" => false, "wait" => false, "code" => nil })
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
# The transfer app as the door sees it: the connection checked or not, the
# kernel's messages.
class FakeApp
  attr_accessor :cert
  attr_reader :sent

  def initialize = (@cert = true; @sent = [])
  def certified? = @cert
  def node_name = "sim"
  def tell_kernel(h) = @sent << h
  def send_message(_to, _type, h) = (@sent << h; true)
end
APP = FakeApp.new
D = R::Door.new(APP)
conf("enabled = true\n")
raises("off", "lets no remote shell in (rshell_enabled is off") { D.open(from: "pc") }
conf("rshell_enabled = true\n[rshell]\nfrom = [\"pc\"]\n")
APP.cert = false
raises("not a checked connection", "only over a checked connection") { D.open(from: "pc") }
conf("rshell_enabled = true\nrshell_uncertified = true\n[rshell]\nfrom = [\"pc\"]\n")
check("rshell_uncertified lets it in", D.open(from: "pc")["id"], 1)
D.drop("test")
APP.cert = true
D.instance_variable_set(:@next_id, 1)
APP.sent.clear
conf("rshell_enabled = true\n")
raises("on, nobody listed", "from pc") { D.open(from: "pc") }
conf("rshell_enabled = true\n[rshell]\nfrom = [\"pc\"]\n")
o = D.open(from: "pc")
check("the mark goes up", APP.sent, [{ "cmd" => "rshell_state", "on" => true }])
check("open", [o["id"], o["cwd"], o["commands"]["cat"], o["version"]["rshell"], o["out_max"]], [1, "/home", "read", 3, 8192])
check("open: get and put are the console's words", [o["commands"].key?("get"), o["commands"].key?("put")], [false, false])
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
check("a replaced session keeps the mark up", APP.sent.size, 1)
Box::Machine.now = 1000 + 2 * R::IDLE_MS + 1
D.tick(Box::Machine.board_millis)
check("idle closes", D.s, nil)
check("the mark goes down", APP.sent.last, { "cmd" => "rshell_state", "on" => false })
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

puts "changing the machine (R2)"
X = R::Session.new(20, "pc", 0, APP)
def w(line) = X.exec(line, true)
def ex?(p) = File.exist?(Box.real(p))
put("/home/w/a.txt", "alpha\n")
put("/home/w/big.bin", "0123456789" * 3000)          # 30000 bytes, 4 chunks
FileUtils.mkdir_p(Box.real("/sd/inbox"))
put("/home/inbox/got.txt", "came\n")
X.exec("cd /home/w", false)

check("exec_read refuses cp", X.exec("cp a.txt b.txt", false)["code"], 126)
check("exec_read refuses rm, nothing done", [X.exec("rm a.txt", false)["code"], ex?("/home/w/a.txt")], [126, true])
check("exec_read refuses kill", X.exec("kill 5", false)["code"], 126)
check("cp", [w("cp a.txt b.txt")["code"], File.read(Box.real("/home/w/b.txt"))], [0, "alpha\n"])
check("cp says what", w("cp -f a.txt b.txt")["out"], "cp: /home/w/a.txt -> /home/w/b.txt (6 bytes)\n")
check("cp: one there, no -f", [w("cp a.txt b.txt")["code"], w("cp a.txt b.txt")["out"].include?("already there (-f")], [1, true])
check("cp a big file a chunk at a time", [w("cp big.bin big2.bin")["code"], File.binread(Box.real("/home/w/big2.bin")) == File.binread(Box.real("/home/w/big.bin"))], [0, true])
check("cp into a folder (over its a.txt)", [w("cp -f a.txt /home/docs")["code"], File.read(Box.real("/home/docs/a.txt"))], [0, "alpha\n"])
check("cp a directory", w("cp /home/docs x")["code"], 1)
check("cp to a folder not there", w("cp a.txt /nowhere/x")["out"], "cp: /nowhere: no such directory\n")
check("cp the same", w("cp a.txt ./a.txt")["code"], 1)
check("cp from a secret", [w("cp /home/wifi.toml w.txt")["out"].include?("kept from"), ex?("/home/w/w.txt")], [true, false])
check("cp from a secret by ..", w("cp ../asterism/config.toml c.txt")["code"], 1)
check("cp to a secret", [w("cp a.txt /home/asterism/x.txt")["out"].include?("kept from"), ex?("/home/asterism/x.txt")], [true, false])
check("cp over wifi.toml", [w("cp -f a.txt /home/wifi.toml")["code"], File.read(Box.real("/home/wifi.toml")).include?("ssid")], [1, true])
check("cp to wifi.toml.part", w("cp a.txt /home/wifi.toml.part")["code"], 1)
check("cp into /home/asterism (a folder)", [w("cp a.txt /home/asterism")["code"], ex?("/home/asterism/a.txt")], [1, false])
check("cp to /flash/home/asterism", w("cp a.txt /flash/home/asterism/k")["code"], 1)
check("cp a bad flag", w("cp -x a.txt b.txt")["code"], 2)
check("mv", [w("mv b.txt c.txt")["code"], ex?("/home/w/b.txt"), ex?("/home/w/c.txt")], [0, false, true])
check("mv onto one there: refused", [w("mv c.txt a.txt")["code"], File.read(Box.real("/home/w/a.txt"))], [1, "alpha\n"])
put("/home/w/c.txt", "gamma\n")
check("mv -f", [w("mv -f c.txt a.txt")["code"], File.read(Box.real("/home/w/a.txt")), ex?("/home/w/c.txt")], [0, "gamma\n", false])
check("mv a folder", [w("mv /home/docs /home/w/docs2")["code"], ex?("/home/w/docs2/a.txt"), ex?("/home/docs")], [0, true, false])
check("mv a folder into itself", w("mv /home/w /home/w/docs2/in")["code"], 1)
check("mv a secret", [w("mv /home/wifi.toml /home/w/x")["code"], ex?("/home/wifi.toml")], [1, true])
check("mv the secret folder", [w("mv /home/asterism /home/w/k")["code"], ex?("/home/asterism/node.tar")], [1, true])
check("mv /home (it holds the secrets)", w("mv /home /h2")["code"], 1)
check("mv onto a secret name", w("mv -f a.txt /etc/secrets.toml")["code"], 1)
check("put: from the inbox", [w("mv /home/inbox/got.txt /home/w/")["code"], File.read(Box.real("/home/w/got.txt"))], [0, "came\n"])
put("/sd/inbox/sd.txt", "sd\n")
check("put: from the SD card's inbox to /home (copied and deleted)",
      [w("mv /sd/inbox/sd.txt /home/w/sd.txt")["code"], File.read(Box.real("/home/w/sd.txt")), ex?("/sd/inbox/sd.txt")], [0, "sd\n", false])
Box::RO << "/ro"
put("/ro/f.txt", "x")
check("rm read-only", [w("rm /ro/f.txt")["out"], ex?("/ro/f.txt")], ["rm: /ro/f.txt: read-only here\n", true])
check("mv from read-only", w("mv /ro/f.txt /home/w/")["out"].include?("read-only"), true)
check("cp to read-only", w("cp a.txt /ro/g.txt")["out"].include?("read-only"), true)
check("mkdir", [w("mkdir n1 n2")["code"], File.directory?(Box.real("/home/w/n1")), File.directory?(Box.real("/home/w/n2"))], [0, true, true])
check("mkdir there", w("mkdir n1")["out"], "mkdir: /home/w/n1: already there\n")
check("mkdir no parent", w("mkdir x/y")["out"], "mkdir: /home/w/x: no such directory\n")
check("mkdir in the secret folder", [w("mkdir /home/asterism/z")["code"], ex?("/home/asterism/z")], [1, false])
check("mkdir in read-only", w("mkdir /ro/d")["code"], 1)
check("rm a file", [w("rm sd.txt")["code"], ex?("/home/w/sd.txt")], [0, false])
check("rm an empty folder", [w("rm n2")["code"], ex?("/home/w/n2")], [0, false])
put("/home/w/n1/deep/er/f.txt", "f")
put("/home/w/n1/g.txt", "g")
check("rm a folder with things: -r", [w("rm n1")["out"], ex?("/home/w/n1/g.txt")], ["rm: /home/w/n1: a directory with things in it (rm -r)\n", true])
check("rm -r", [w("rm -r n1")["code"], ex?("/home/w/n1")], [0, false])
check("rm nothing", w("rm nothing")["code"], 1)
check("rm several, one missing", [w("rm got.txt nothing")["code"], ex?("/home/w/got.txt")], [1, false])
check("rm wifi.toml", [w("rm /home/wifi.toml")["code"], ex?("/home/wifi.toml")], [1, true])
check("rm in the secret folder", [w("rm /home/asterism/node.tar")["code"], ex?("/home/asterism/node.tar")], [1, true])
check("rm -r the secret folder", [w("rm -r /home/asterism")["code"], ex?("/home/asterism/config.toml")], [1, true])
check("rm -r /home", [w("rm -r /home")["code"], ex?("/home/memo.txt")], [1, true])
check("rm -r /", [w("rm -r /")["code"], w("rm -r /flash/")["code"], w("rm -r /etc")["code"]], [1, 1, 1])
check("rm by ..", [w("rm -r ../../home")["code"], ex?("/home/memo.txt")], [1, true])
check("base and dir of a UTF-8 name", [R.base("/home/あ/い.txt"), R.dir("/home/あ/い.txt"), R.dir("/x"), R.base("/")], ["い.txt", "/home/あ", "/", ""])
put("/home/w/かな.txt", "k")
check("cp a UTF-8 name into a folder", [w("cp かな.txt /home/w/docs2")["code"], ex?("/home/w/docs2/かな.txt")], [0, true])
check("holds_secret?", [R.holds_secret?("/"), R.holds_secret?("/home"), R.holds_secret?("/flash/home"),
                        R.holds_secret?("/home/asterism/x"), R.holds_secret?("/home/w"), R.holds_secret?("/etc/x")],
      [true, true, true, true, false, false])

puts "kill and open (R2)"
APP.sent.clear
check("kill: no such app", w("kill 99")["code"], 1)
check("kill: a free slot is no app", w("kill Gone")["code"], 1)
check("kill: the desktop", [w("kill 2")["out"], APP.sent], ["kill: system_desktop: a system task, not ended from here\n", []])
check("kill: the kernel", w("kill kernel")["code"], 1)
check("kill: itself", w("kill Transfer")["out"], "kill: Transfer: the transfer app holds this session\n")
check("kill: two by the name", w("kill Clock")["out"].include?("2 apps are named Clock"), true)
a = w("kill Note\\ Pad")
check("kill: asked", [a["code"], a["wait"], a["more"], APP.sent.last], [0, true, a["out"].bytesize, { "cmd" => "kill", "pid" => 5 }])
check("kill: asked, says so", a["out"], "kill: asked the kernel to end PID 5 (Note Pad)\n")
m = X.more(a["more"])
check("kill: nothing yet", [m["out"], m["more"], m["wait"], m["code"]], ["", a["more"], true, nil])
X.note({ "cmd" => "kill_result", "pid" => 9, "ok" => false })
check("kill: another pid's answer is not ours", X.more(a["more"])["wait"], true)
X.note({ "cmd" => "kill_result", "pid" => 5, "ok" => true, "info" => "Note Pad" })
check("kill: the kernel asked it", X.more(a["more"])["wait"], true)
X.note({ "cmd" => "died", "pid" => 5 })
m = X.more(a["more"])
check("kill: ended", [m["out"], m["more"], m["wait"], m["code"]], ["kill: PID 5 (Note Pad) ended\n", nil, false, 0])
a = w("kill 6")
X.note({ "cmd" => "kill_result", "pid" => 6, "ok" => false, "info" => "suspended" })
m = X.more(a["more"])
check("kill: refused", [m["out"], m["code"]], ["kill: PID 6 (Clock): refused by the kernel (suspended)\n", 1])
Box::Machine.now = 10_000_000
a = w("kill 7")
X.note({ "cmd" => "kill_result", "pid" => 7, "ok" => true })
Box::Machine.now = 10_000_000 + R::WAIT_MS
m = X.more(a["more"])
check("kill: asked, not ended in time", [m["out"].include?("has not in 5 s"), m["code"]], [true, 1])
a = w("kill 7")
Box::Machine.now = 10_000_000 + 3 * R::WAIT_MS
check("kill: no answer at all", X.more(a["more"])["out"], "kill: no answer from the kernel in 5 s (ps)\n")
check("the next command forgets the wait", [w("pwd")["out"], X.more(0)["wait"]], ["/home/w\n", false])
put("/home/w/game.app.rb", "class G < FmrbApp; end\n")
put("/home/w/tool.app.wasm", "\0asm")
check("open: not an app", w("open a.txt")["out"], "open: /home/w/a.txt: not an app (.app.rb, .app.wasm)\n")
check("open: not there", w("open none.app.rb")["code"], 1)
check("open: a secret", w("open /home/asterism/x.app.rb")["code"], 1)
a = w("open game.app.rb")
check("open: asked", [a["code"], a["wait"], APP.sent.last], [0, true, { "cmd" => "spawn", "app_name" => "/home/w/game.app.rb" }])
X.note({ "cmd" => "spawn_result", "app" => "/usr/share/transfer/install_ask.app.rb", "pid" => 9 })
check("open: another app's start is not ours", X.more(a["more"])["wait"], true)
X.note({ "cmd" => "spawn_result", "app" => "/home/w/game.app.rb", "pid" => 9 })
m = X.more(a["more"])
check("open: started", [m["out"], m["code"]], ["open: /home/w/game.app.rb started (PID 9)\n", 0])
a = w("open tool.app.wasm")
X.note({ "cmd" => "spawn_failed", "app" => "/home/w/tool.app.wasm", "reason" => "no memory" })
m = X.more(a["more"])
check("open: not started", [m["out"], m["code"]], ["open: /home/w/tool.app.wasm: not started (no memory)\n", 1])
check("open: the whole text from 0", X.more(0)["out"], "open: asked the kernel to start /home/w/tool.app.wasm\nopen: /home/w/tool.app.wasm: not started (no memory)\n")
check("door: note reaches the session", R::Door.instance_method(:note).arity, 1)
nosend = R::Session.new(30, "pc", 0, nil)
check("kill with no kernel to ask", nosend.exec("kill 5", true)["out"], "kill: the kernel cannot be reached\n")

puts "the completion of kill and open (R2)"
check("kill: the apps by name", X.complete("kill ", nil)["items"], ["Clock", "Note\\ Pad"])
check("kill: N", X.complete("kill N", nil)["items"], ["Note\\ Pad"])
put("/home/w/apps/x.app.rb", "")
check("open: folders and apps only", X.complete("open ", nil)["items"].sort,
      ["a.txt", "apps/", "big.bin", "big2.bin", "c.txt", "docs2/", "game.app.rb", "tool.app.wasm"].select { |n| n.end_with?("/") || n.include?(".app.") })
check("open: in a folder", X.complete("open apps/", nil)["items"], ["apps/x.app.rb"])
check("cp: every name", X.complete("cp a", nil)["items"], ["a.txt", "apps/"])

puts "put: write_begin, write_chunk, write_end (R4)"
P = R::Session.new(40, "pc", 0, APP)
P.exec("cd /home/w", false)
bin = (0..255).map(&:chr).join.b * 100                        # 25600 bytes, every byte
b = P.write_begin("up.bin", bin.bytesize, false)
check("write_begin", [b["path"], b["size"], b["pos"], b["piece"], ex?("/home/w/.up.bin.part"), ex?("/home/w/up.bin")],
      ["/home/w/up.bin", 25600, 0, 8192, true, false])
check("write_chunk", P.write_chunk(0, bin.byteslice(0, 8192)), 8192)
check("a piece asked again is read past", P.write_chunk(0, bin.byteslice(0, 8192)), 8192)
check("a piece after the position is not taken", P.write_chunk(16384, bin.byteslice(16384, 8192)), 8192)
check("a piece over the position: its tail", P.write_chunk(4096, bin.byteslice(4096, 8192)), 12288)
raises("write_end before all is in", "12288 of 25600 bytes are in") { P.write_end }
check("the rest", [P.write_chunk(12288, bin.byteslice(12288, 8192)), P.write_chunk(20480, bin.byteslice(20480, 5120))], [20480, 25600])
check("not there before the end", ex?("/home/w/up.bin"), false)
e = P.write_end
check("write_end", [e, File.binread(Box.real("/home/w/up.bin")) == bin, ex?("/home/w/.up.bin.part")],
      [{ "path" => "/home/w/up.bin", "size" => 25600 }, true, false])
raises("write_chunk with none begun", "no file is coming in") { P.write_chunk(0, "x") }
raises("one there, no overwrite", "already there") { P.write_begin("up.bin", 3, false) }
P.write_begin("up.bin", 3, true)
P.write_chunk(0, "new")
P.write_end
check("overwrite", File.binread(Box.real("/home/w/up.bin")), "new")
P.write_begin("empty.txt", 0, false)
check("an empty file", [P.write_end["size"], File.size(Box.real("/home/w/empty.txt"))], [0, 0])
P.write_begin("かな.txt", 6, true)
P.write_chunk(0, "かな")
check("a UTF-8 name", [P.write_end["path"], File.read(Box.real("/home/w/かな.txt"))], ["/home/w/かな.txt", "かな"])
raises("a piece too large", "at most 8192 bytes") { P.write_begin("x.bin", 9000, false); P.write_chunk(0, "x" * 8193) }
raises("past the size", "goes past 9000 bytes") { P.write_chunk(0, "x" * 8000); P.write_chunk(8000, "x" * 1001) }
check("past the size: the piece file goes", ex?("/home/w/.x.bin.part"), false)
raises("a secret", "kept from") { P.write_begin("/home/asterism/k", 1, true) }
raises("a secret by ..", "kept from") { P.write_begin("../asterism/config.toml", 1, true) }
raises("wifi.toml", "kept from") { P.write_begin("/home/wifi.toml", 1, true) }
raises("/home itself", "kept from") { P.write_begin("/home", 1, true) }
raises("read-only", "read-only") { P.write_begin("/ro/x", 1, false) }
raises("a directory there", "a directory there") { P.write_begin("/home/w/apps", 1, true) }
raises("no such folder", "no such directory") { P.write_begin("/nowhere/x", 1, false) }
raises("does not fit", "do not fit") { P.write_begin("/tmp/x", 200_000, false) }
raises("not a size", "not a size") { P.write_begin("y", -1, false) }
P.write_begin("c1.bin", 10, false)
P.write_chunk(0, "12345")
check("write_cancel: the piece file goes", [P.write_cancel, ex?("/home/w/.c1.bin.part"), ex?("/home/w/c1.bin")], [true, false, false])
P.write_begin("c2.bin", 10, false)
P.write_begin("c3.bin", 10, false)
check("another write_begin: the first goes", [ex?("/home/w/.c2.bin.part"), ex?("/home/w/.c3.bin.part")], [false, true])
P.close
check("close: the piece file goes", ex?("/home/w/.c3.bin.part"), false)
Box::Machine.now = 50_000_000
P.write_begin("c4.bin", 10, false)
P.write_tick(50_000_000 + R::WRITE_IDLE_MS - 1)
check("kept while it may go on", ex?("/home/w/.c4.bin.part"), true)
P.write_tick(50_000_000 + R::WRITE_IDLE_MS)
check("not gone on with: it goes", ex?("/home/w/.c4.bin.part"), false)

puts "a long cp, a piece at a time (R4)"
put("/home/w/large.bin", (0..255).map(&:chr).join.b * 1024)   # 256 KB
Box::Machine.auto = 10
a = w("cp large.bin large2.bin")
check("cp large: asked", [a["code"], a["wait"], a["out"]], [0, true, "cp: /home/w/large.bin -> /home/w/large2.bin: copying 256 KB\n"])
m = X.more(a["more"])
check("cp large: nothing yet, progress", [m["out"], m["wait"], m["progress"]], ["", true, { "done" => 0, "total" => 262144 }])
steps = 0
steps += 1 while X.step(Box::Machine.board_millis) && steps < 1000
check("cp large: in several steps", steps > 1, true)
check("cp large: hidden while it copies, gone after", ex?("/home/w/.large2.bin.part"), false)
m = X.more(a["more"])
check("cp large: done", [m["out"], m["wait"], m["code"], m["more"]], ["cp: /home/w/large.bin -> /home/w/large2.bin (262144 bytes)\n", false, 0, nil])
check("cp large: the same bytes", File.binread(Box.real("/home/w/large2.bin")) == File.binread(Box.real("/home/w/large.bin")), true)
check("cp large: one there, no -f", w("cp large.bin large2.bin")["code"], 1)
a = w("cp -f large.bin large2.bin")
X.step(Box::Machine.board_millis)
check("cp large: under way", [ex?("/home/w/.large2.bin.part"), X.more(a["more"])["progress"]["done"] > 0], [true, true])
X.stop
m = X.more(a["more"])
check("cp large: stopped", [m["out"].include?("stopped"), m["code"], ex?("/home/w/.large2.bin.part")], [true, 1, false])
a = w("cp large.bin large3.bin")
X.step(Box::Machine.board_millis)
check("another command stops the copy", [w("pwd")["out"], ex?("/home/w/.large3.bin.part"), ex?("/home/w/large3.bin")], ["/home/w\n", false, false])
a = w("cp large.bin large3.bin")
X.close
check("close stops the copy", [ex?("/home/w/.large3.bin.part"), ex?("/home/w/large3.bin")], [false, false])
put("/home/w/large.bin", (0..255).map(&:chr).join.b * 1024)
a = w("mv large.bin /sd/inbox/large.bin")
check("mv large to another storage: copied a piece at a time", a["wait"], true)
nil while X.step(Box::Machine.board_millis)
m = X.more(a["more"])
check("mv large: done", [m["out"], m["code"], ex?("/home/w/large.bin"), File.size(Box.real("/sd/inbox/large.bin"))],
      ["mv: /home/w/large.bin -> /sd/inbox/large.bin\n", 0, false, 262144])
check("get: a binary file through read", P.read("/home/w/large2.bin", 0, 8192)["data"].b == ((0..255).map(&:chr).join.b * 32), true)
check("small cp still in the call", w("cp a.txt a2.txt")["wait"], nil)
Box::Machine.auto = 0
conf("rshell_enabled = true\n[rshell]\nfrom = [\"pc\"]\n")
d = D.open(from: "pc")
check("door: tick is busy just after a call", D.tick(Box::Machine.board_millis), true)
check("door: tick is idle later", D.tick(Box::Machine.board_millis + R::BUSY_FOR_MS), false)
D.write_begin(d["id"], "door.txt", 2, false, from: "pc")
D.write_chunk(d["id"], 0, "ok", from: "pc")
check("door: put", [D.write_end(d["id"], from: "pc")["path"], File.read(Box.real("/home/door.txt"))], ["/home/door.txt", "ok"])
check("door: write_cancel, stop", [D.write_cancel(d["id"], from: "pc"), D.stop(d["id"], from: "pc")], [true, true])
raises("door: a node not let in writes nothing", "lets no remote shell in from other") { D.write_begin(d["id"], "x", 1, false, from: "other") }
D.drop("test")

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

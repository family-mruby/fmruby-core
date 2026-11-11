#!/usr/bin/env ruby
# Host tests for FmrbReceiveConf (flash/usr/share/transfer/receive_conf.rb,
# doc/asterism_file_transfer T6, T7): where receive.toml is read from
# (/home, else /etc), the switch (enabled; no line is off), the changes the
# Receiving app and the shell's areceive make (the part by hand only, line
# by line; the console's part and the mark as they were; written only when
# the text changes; the first time from the copy in /etc), what areceive
# shows ("!" on a checkpoint list with "*"), and that the kernel's own
# reader of the switches (fmrb_kernel.rb receiving_enabled?,
# sending_enabled? and transfer_conf_on?, a Spinel program that cannot load
# the module) answers as the module does. The second switch, send_enabled
# (doc/window_menu M2), is read and changed the same way.
#
# The shipped files run here with their paths moved into a temporary
# directory; FmrbToml is the firmware's reader (test/toml/host_toml.rb).
#
#   ruby test/receive_conf/run.rb      (rake receive_conf:test, in rake test)

require "tmpdir"
require "fileutils"
require_relative "../toml/host_toml"

module Check
  @failed = 0
  def self.failed = @failed
  def self.fail! = @failed += 1
end

def check(what, got, want)
  if got == want
    puts "  ok   #{what}"
  else
    puts "  FAIL #{what}: got #{got.inspect}, want #{want.inspect}"
    Check.fail!
  end
end

def raises(what, pattern)
  yield
  puts "  FAIL #{what}: no error"
  Check.fail!
rescue ArgumentError => e
  check(what, e.message.match?(pattern), true)
end

ROOT = File.expand_path("../..", __dir__)
TMP = Dir.mktmpdir("fmrb_receive_conf_test")
at_exit { FileUtils.rm_rf(TMP) }
HOME = "#{TMP}/home/asterism"
ETC = "#{TMP}/etc/asterism"
FileUtils.mkdir_p(ETC)

src = File.read("#{ROOT}/flash/usr/share/transfer/receive_conf.rb")
          .gsub("/home/asterism", HOME).gsub("/etc/asterism", ETC)
eval(src, TOPLEVEL_BINDING, "receive_conf.rb")

# The kernel's reader, its method as it ships, on a stand-in with its two
# constants.
kernel = File.read("#{ROOT}/main/prebuild_scripts/kernel/fmrb_kernel.rb")
class KernelStandIn
  RECEIVE_CONF = "#{HOME}/receive.toml"
  RECEIVE_CONF_SYS = "#{ETC}/receive.toml"
end
["receiving_enabled\\?", "sending_enabled\\?", "transfer_conf_path", "transfer_conf_on\\?\\(key\\)"].each do |name|
  body = kernel[/^  def #{name}\n.*?^  end\n/m] or abort "no #{name} in fmrb_kernel.rb"
  KernelStandIn.class_eval(body)
end
KERNEL = KernelStandIn.new

C = FmrbReceiveConf
USER = "#{HOME}/receive.toml"
SYS = "#{ETC}/receive.toml"
SHIPPED = File.read("#{ROOT}/flash/etc/asterism/receive.toml")
MANAGED = "# ---- managed by the console (below this line; edit above it) ----\n" \
          "[managed]\nby = \"console\"\nat = \"2026-11-09T00:00:00Z\"\n\n" \
          "[managed.file]\nfrom = [\"fmruby-*\"]\n\n[managed.checkpoint]\nfrom = []\n"

def put(path, text)
  FileUtils.mkdir_p(File.dirname(path))
  File.write(path, text)
end

def reset(user: nil, sys: SHIPPED)
  FileUtils.rm_rf(HOME)
  FileUtils.rm_f(SYS)
  put(SYS, sys) if sys
  put(USER, user) if user
end

def both(what, want)
  check("#{what}: FmrbReceiveConf", C.enabled?, want)
  check("#{what}: the kernel", KERNEL.receiving_enabled?, want)
end

def both_send(what, want)
  check("#{what}: FmrbReceiveConf (send)", C.send_enabled?, want)
  check("#{what}: the kernel (send)", KERNEL.sending_enabled?, want)
end

puts "where it is read, the switch"
reset(sys: nil)
check("no file at all: no path", C.path, nil)
both("no file at all", false)
check("no file: lines", C.lines.first.start_with?("receiving: off (no "), true)
reset
check("the shipped copy only: /etc", C.path, SYS)
both("the shipped copy (enabled = false)", false)
check("the shipped copy parses", FmrbToml.parse(SHIPPED).is_a?(Hash), true)
check("the shipped copy lists nobody", [C.view["file"]["hand"], C.view["checkpoint"]["hand"]], [[], []])
reset(user: "[file]\nfrom = [\"pc\"]\n")
check("a file in /home: /home", C.path, USER)
both("a file by hand with no enabled line", false)
check("refused in the transfer app's terms", C.senders("file")["enabled"], false)
reset(user: "# mine\nenabled = true   # on\n\n[file]\nfrom = [\"pc\"]\n")
both("enabled = true with a comment", true)
reset(user: "enabled = \"true\"\n")
both("enabled = \"true\" (a string)", false)
reset(user: "[file]\nenabled = true\nfrom = []\n")
both("enabled inside a table", false)
reset(user: MANAGED)
both("only the console's part", false)
reset(user: "enabled = true\n\n#{MANAGED}")
both("on, with the console's part", true)
check("the console's list is read", C.senders("file")["managed"], ["fmruby-*"])
reset(user: "enabled = false\n", sys: "enabled = true\n")
both("/home off wins over /etc on", false)

puts "turning it on and off"
reset
check("on from the shipped copy: written", C.set_enabled(true), true)
check("the user's file is made", File.exist?(USER), true)
check("the shipped comments are kept", File.read(USER).start_with?("# Who may send files and apps"), true)
check("only the switch changed", File.read(USER), SHIPPED.sub("enabled = false", "enabled = true"))
both("after on", true)
check("on again: not written", C.set_enabled(true), false)
check("off: written", C.set_enabled(false), true)
both("after off", false)
reset(user: "# by hand\n[file]\nfrom = [\"pc\"]\n\n#{MANAGED}")
check("on, a file with no enabled line", C.set_enabled(true), true)
text = File.read(USER)
check("the line goes after the opening comments, before the first table",
      text.start_with?("# by hand\nenabled = true\n\n[file]\n"), true)
check("the console's part and the mark as they were", text.end_with?("\n\n#{MANAGED}"), true)
both("on, a file with no enabled line", true)
reset(user: MANAGED)
C.set_enabled(true)
check("on, a file with the console's part only", File.read(USER), "enabled = true\n\n#{MANAGED}")
reset(user: "enabled = [\n")
raises("a part by hand that is not TOML is not rewritten", /not TOML/) { C.set_enabled(true) }
check("... and stays as it was", File.read(USER), "enabled = [\n")

puts "the send switch (doc/window_menu M2)"
reset
both_send("the shipped copy (send_enabled = false)", false)
check("the shipped copy has the line", SHIPPED.include?("\nsend_enabled = false\n"), true)
check("sending off in the lines", C.lines[1].start_with?("sending: off"), true)
check("send on from the shipped copy: written", C.set_send_enabled(true), true)
check("only the send switch changed", File.read(USER), SHIPPED.sub("send_enabled = false", "send_enabled = true"))
both_send("after send on", true)
both("receiving stays off", false)
check("view", [C.view["enabled"], C.view["send_enabled"]], [false, true])
check("send on again: not written", C.set_send_enabled(true), false)
check("sending on in the lines", C.lines[1].start_with?("sending: on"), true)
check("receive on: written", C.set_enabled(true), true)
both("both on: receiving", true)
both_send("both on: sending", true)
check("send off: written", C.set_send_enabled(false), true)
both_send("after send off", false)
both("receiving still on", true)
reset(user: "# mine\n[file]\nfrom = [\"pc\"]\n")
check("send on, a file with no switch lines", C.set_send_enabled(true), true)
check("the line goes before the first table", File.read(USER).start_with?("# mine\nsend_enabled = true\n\n[file]\n"), true)
both_send("send on, a file with no switch lines", true)
both("no enabled line: receiving off", false)
reset(user: "send_enabled = true # keep the list\nenabled = false\n")
both_send("send_enabled = true with a comment", true)
both("enabled = false beside it", false)
reset(user: "enabled = true\n")
both_send("no send_enabled line", false)
reset(user: "[x]\nsend_enabled = true\n")
both_send("send_enabled inside a table", false)

puts "allow and deny"
reset
check("allow file pc-desk", C.allow("file", "pc-desk"), true)
check("the list by hand", C.view["file"]["hand"], ["pc-desk"])
check("again: not written", C.allow("file", "pc-desk"), false)
check("allow file fmruby-*", C.allow("file", "fmruby-*"), true)
check("the line", File.read(USER).include?("[file]\nfrom = [\"pc-desk\", \"fmruby-*\"]\n"), true)
check("still off", C.enabled?, false)
raises("a name with a space", /not a node ID/) { C.allow("file", "a b") }
raises("a quote", /not a node ID/) { C.allow("file", "a\"b") }
raises("an unknown kind", /not a kind/) { C.allow("probe", "pc") }
raises("deny a node not there", /not in the file list/) { C.deny("file", "nobody") }
check("deny pc-desk", C.deny("file", "pc-desk"), true)
check("the list by hand after", C.view["file"]["hand"], ["fmruby-*"])
reset(user: "enabled = true\n# who\n[file]\nfrom = [\n  \"a\",   # first\n  \"b\",\n]\nkeep = 1\n\n#{MANAGED}")
check("allow into a list over several lines", C.allow("file", "c"), true)
text = File.read(USER)
check("the list is one line now, the next key kept",
      text.include?("[file]\nfrom = [\"a\", \"b\", \"c\"]\nkeep = 1\n"), true)
check("the comment above the table kept", text.include?("# who\n[file]"), true)
check("the console's part as it was", text.end_with?(MANAGED), true)
raises("deny a node of the console's list", /changed there/) { C.deny("file", "fmruby-*") }
reset(user: "enabled = true\nfrom = [\"old\"]\n")
check("checkpoint: allow into a file with the older form", C.allow("checkpoint", "new"), true)
check("the older top-level from stays",
      File.read(USER), "enabled = true\nfrom = [\"old\"]\n\n[checkpoint]\nfrom = [\"new\"]\n")
check("checkpoint senders", C.view["checkpoint"]["hand"], %w[new old])
check("deny one of the older form", C.deny("checkpoint", "old"), true)
check("both lists", C.view["checkpoint"]["hand"], ["new"])
check("the file still TOML", FmrbToml.parse(File.read(USER)).is_a?(Hash), true)
reset(user: "enabled = true\n[file]\nfrom = [\"x\"]\n")
check("a table added at the end", C.allow("checkpoint", "y"), true)
check("the text", File.read(USER), "enabled = true\n[file]\nfrom = [\"x\"]\n\n[checkpoint]\nfrom = [\"y\"]\n")

puts "what areceive shows"
reset(user: "enabled = true\n[file]\nfrom = [\"*\"]\n[checkpoint]\nfrom = [\"*\"]\n\n#{MANAGED}")
l = C.lines
check("on and the file", l[0], "receiving: on (#{USER})")
check("everyone, files", l.include?("  *  by hand (everyone on the network)"), true)
check("everyone, checkpoint: no mark", l.include?("checkpoint:") && l.count("  *  by hand (everyone on the network)") == 2, true)
check("everyone, checkpoint: the note",
      l.include?("note: everyone on the network may move apps here"), true)
check("no ! anywhere", l.any? { _1.start_with?("!") }, false)
check("the console's part", l.include?("  fmruby-*  from console"), true)
reset(user: "enabled = true\n[file]\nfrom = [\"pc\"]\n")
check("no note without * on checkpoint", C.lines.any? { _1.start_with?("note:") }, false)
reset
check("off and the file", C.lines[0].start_with?("receiving: off (#{SYS})"), true)

puts "the console's part written (T6) on a machine with no file of its own"
reset
check("written", C.write_managed("console", "2026-11-10T00:00:00Z", { "file" => ["a"], "checkpoint" => [] }), true)
text = File.read(USER)
check("the part by hand is the shipped copy (off)", text.start_with?(SHIPPED), true)
both("still off", false)
check("again: not written", C.write_managed("console", "2026-11-10T00:00:00Z", { "file" => ["a"], "checkpoint" => [] }), false)

puts(Check.failed.zero? ? "receive_conf: all ok" : "receive_conf: #{Check.failed} FAILED")
exit(Check.failed.zero? ? 0 : 1)

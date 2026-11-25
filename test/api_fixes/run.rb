#!/usr/bin/env ruby
# Host tests for the API fixes found while writing sig/ (doc/api_fixes F1,
# F2).
# Each runs the real Ruby file under host Ruby with the little it needs
# stubbed:
#
#   - File.path (class method) returns its argument
#     (lib/gems/picoruby-fmrb-filesystem/mrblib/file.rb, loaded into a module
#     so it does not replace the host's own File)
#   - JSON::Digger#dig raises past the end of an Array, JSON.parse makes
#     \uXXXX a UTF-8 character, and JSON.generate refuses NaN and the
#     infinities (lib/upstream/patch/picoruby-json/mrblib/json.rb)
#   - MIDI::Device#pitch_bend works without Integer#clamp
#     (lib/gems/picoruby-fmrb-midi/mrblib/fmrb-midi.rb over picoruby-midi)
#   - SmfPlayer stops a note at the pitch it started at when transpose
#     changes in between (fmrb-smf.rb)
#   - MmlPlayer#load_text clears "no notes" once a later part has notes
#     (fmrb-mml.rb)
#   - IO#puts writes an Array one line per element, as mruby does, and so do
#     the shell's stand-ins for $stdout (fmrb-io's io.rb, shell_io.rb)
#   - FmrbUI#bg= moves the widgets that took the container's colour, and
#     leaves one the app gave its own (fmrb-ui.rb)
#   - Two MIDI players in one app: stopping one drops only its own queued
#     commands (doc/api_fixes F3; fmrb-midi.rb, fmrb-mml.rb, fmrb-smf.rb over
#     a stand-in for the C queue that keeps the owner the way
#     fmrb_midi_sched.c does)
#   - A player's playing? ends with its own queued commands, not with the
#     whole queue (doc/api_fixes F4, C3)
#   - The pin table's GPIO rule on every board: the buttons are input only,
#     the other system pins and another app's pins refuse GPIO (C, compiled
#     here with gcc: test/api_fixes/pin_ledger)
#
# The C halves (File.open modes, Kernel.send_message) are checked on the sim
# (doc/api_fixes/report/f1.md, f3.md).
#
#   ruby test/api_fixes/run.rb        (or: rake api_fixes:test)

ROOT = File.expand_path("../..", __dir__)

$failures = 0
$count = 0

def check(name, got, want)
  $count += 1
  return if got == want

  $failures += 1
  puts "FAIL #{name}: got #{got.inspect}, want #{want.inspect}"
end

def raised(&blk)
  blk.call
  nil
rescue => e
  [e.class.name, e.message]
end

# ---- File.path ---------------------------------------------------------------

FileGem = Module.new
FileGem.module_eval(File.read(File.join(ROOT, "lib/gems/picoruby-fmrb-filesystem/mrblib/file.rb")))
check("File.path(String)", FileGem::File.path("x"), "x")
check("File.path(nil)", raised { FileGem::File.path(nil) }, ["ArgumentError", "missing filename"])
check("File.path(Integer)", raised { FileGem::File.path(1) },
      ["TypeError", "no implicit conversion of Integer into String"])

# ---- JSON --------------------------------------------------------------------

load File.join(ROOT, "lib/upstream/patch/picoruby-json/mrblib/json.rb")

j = '[{"device":{"name":"Remo"}}, {"device":{"name":null}}]'
range = ["JSON::DiggerError", "Array index out of range"]
check("dig in range", JSON::Digger.new(j).dig(0, "device", "name").parse, "Remo")
check("dig last", JSON::Digger.new(j).dig(1, "device", "name").parse, nil)
check("dig one past", raised { JSON::Digger.new(j).dig(2) }, range)
check("dig far past", raised { JSON::Digger.new(j).dig(3, "device", "name") }, range)
check("dig [1] 5", raised { JSON::Digger.new("[1]").dig(5) }, range)
check("dig [1] 1", raised { JSON::Digger.new("[1]").dig(1) }, range)
check("dig [] 0", raised { JSON::Digger.new("[]").dig(0) }, range)
check("dig [ ] 0", raised { JSON::Digger.new("[ ]").dig(0) }, range)
check("dig nested", JSON::Digger.new("[[1,2],[3]]").dig(1, 0).parse, 3)
check("dig nested past", raised { JSON::Digger.new("[[1,2],[3]]").dig(1, 1) }, range)
# Unchanged: a negative position and a missing key.
check("dig negative", raised { JSON::Digger.new(j).dig(-1) },
      ["ArgumentError", "Negative index is not supported"])
check("dig missing key", raised { JSON::Digger.new(j).dig(1, "___device") },
      ["JSON::DiggerError", "Key not found: ___device"])

# Bytes, so the host's string encodings do not come into it.
def ubytes(json)
  JSON.parse(json).bytes
end
# The escapes are built from a backslash at run time so that no tool on
# the way to this file turns them into the characters themselves.
BS = 92.chr
def esc(*hex) = hex.map { |h| "#{BS}u#{h}" }.join
check("u0041", ubytes("\"#{esc("0041")}\""), [0x41])
check("u00e9", ubytes("\"#{esc("00e9")}\""), [0xC3, 0xA9])
check("u3042", ubytes("\"#{esc("3042")}\""), [0xE3, 0x81, 0x82])
check("u3042 in text", ubytes("\"x#{esc("3042")}y\""), [0x78, 0xE3, 0x81, 0x82, 0x79])
check("surrogate pair", ubytes("\"#{esc("d83d", "de00")}\""), [0xF0, 0x9F, 0x98, 0x80])
check("u cut short", raised { JSON.parse("\"#{BS}u30\"") },
      ["JSON::ParserError", "Incomplete unicode escape sequence"])

# JSON.generate refuses NaN and the infinities, nested too (doc/api_fixes F4)
nan = 0.0 / 0.0
inf = 1.0 / 0.0
check("generate NaN", raised { JSON.generate(nan) }, ["JSON::GeneratorError", "NaN not allowed in JSON"])
check("generate Infinity", raised { JSON.generate(inf) },
      ["JSON::GeneratorError", "Infinity not allowed in JSON"])
check("generate -Infinity", raised { JSON.generate(-inf) },
      ["JSON::GeneratorError", "-Infinity not allowed in JSON"])
check("generate nested NaN", raised { JSON.generate({ "a" => { "b" => [1, nan] } }) },
      ["JSON::GeneratorError", "NaN not allowed in JSON"])
check("GeneratorError is a JSONError", JSON::GeneratorError.ancestors.include?(JSON::JSONError), true)
check("generate finite floats", JSON.generate([1, 1.5, -0.25, { "x" => 2.0 }]), '[1,1.5,-0.25,{"x":2.0}]')

# ---- MIDI --------------------------------------------------------------------

module FmrbMidi
  CC_DUTY = 70
  CC_VOLUME = 7
  CH_PULSE1 = 0
  CH_PULSE2 = 1
  CH_TRIANGLE = 2
  CH_NOISE = 3
  def self.scheduler = nil
  def self.next_owner = 1
end

load File.join(ROOT, "lib/gems/picoruby-midi/mrblib/midi_constants.rb")
load File.join(ROOT, "lib/gems/picoruby-midi/mrblib/midi_device.rb")
# Only the MIDI::Device reopening at the end of fmrb-midi.rb is wanted; the
# rest needs the APU. Take the file from that reopening on.
midi_src = File.read(File.join(ROOT, "lib/gems/picoruby-fmrb-midi/mrblib/fmrb-midi.rb"))
eval(midi_src[midi_src.index("\nmodule MIDI\n")..], binding, "fmrb-midi.rb")

class RecordingTransport
  attr_reader :packets

  def initialize = (@packets = [])
  def send_packet(*a) = @packets << a
end

# The build has no Integer#clamp (mruby-comparable-ext is left out).
saved_clamp = Comparable.instance_method(:clamp)
Comparable.send(:remove_method, :clamp)
begin
  t = RecordingTransport.new
  dev = MIDI::Device.new(t)
  dev.pitch_bend(0)
  dev.pitch_bend(8191, channel: 2)
  dev.pitch_bend(-9000)
  dev.pitch_bend(9000)
  check("pitch_bend packets", t.packets.map { |p| p[2..4] },
        [[0xE0, 0x00, 0x40], [0xE2, 0x7F, 0x7F], [0xE0, 0x00, 0x00], [0xE0, 0x7F, 0x7F]])
ensure
  Comparable.send(:define_method, :clamp, saved_clamp)
end

load File.join(ROOT, "lib/gems/picoruby-fmrb-midi/mrblib/fmrb-smf.rb")

class NoteDevice
  attr_reader :log

  def initialize = (@log = [])
  def send_note_on(ch, n, v) = @log << [:on, ch, n]
  def send_note_off(ch, n) = @log << [:off, ch, n]
end

smf = FmrbMidi::SmfPlayer.allocate
d = NoteDevice.new
smf.instance_variable_set(:@device, d)
smf.instance_variable_set(:@sounding, [])
smf.transpose = 0
smf.send(:send_note_on, 0, 60, 100)
smf.transpose = 2
smf.send(:send_note_on, 1, 60, 100)
smf.send(:send_note_off, 0, 60)   # started untransposed: off at 60
smf.send(:send_note_on, 9, 36, 100)
smf.send(:send_note_off, 1, 60)   # started at +2: off at 62
smf.transpose = -1
smf.send(:send_note_off, 9, 36)   # drums are never moved
smf.send(:send_note_off, 0, 64)   # never started here: as now shifted
check("transpose note offs", d.log,
      [[:on, 0, 60], [:on, 1, 62], [:off, 0, 60], [:on, 9, 36], [:off, 1, 62],
       [:off, 9, 36], [:off, 0, 63]])
check("transpose sounding empty", smf.instance_variable_get(:@sounding), [])
smf.transpose = 5
smf.send(:send_note_on, 3, 70, 100)
d.log.clear
smf.send(:silence)
check("silence", d.log, [[:off, 3, 75]])

load File.join(ROOT, "lib/gems/picoruby-midi-mml/mrblib/midi_mml.rb")
load File.join(ROOT, "lib/gems/picoruby-fmrb-midi/mrblib/fmrb-mml.rb")

mml = FmrbMidi::MmlPlayer.new(nil)
check("mml empty then notes", [mml.load_text("r4\no4 c4"), mml.error], [true, nil])
check("mml notes", [mml.load_text("o4 c4"), mml.error], [true, nil])
check("mml no notes", [mml.load_text("r4"), mml.error], [false, "no notes in the MML"])
check("mml no parts", [mml.load_text("bpm 90"), mml.error], [false, "no parts in the file"])

# ---- IO#puts (doc/api_fixes F2) ---------------------------------------------
#
# lib/gems/picoruby-fmrb-io/mrblib/io.rb and the shell's two stand-ins for
# $stdout (main/ruby/default_app/shell/shell_io.rb), loaded into modules so
# the host's own IO stays as it is. Expectations are mruby's io_puts
# (mruby-io/src/io.c): an Array one line per element, nested too, [] a
# newline, 16 levels deep "[...]".

IoGem = Module.new
IoGem.module_eval(File.read(File.join(ROOT, "lib/gems/picoruby-fmrb-io/mrblib/io.rb")))
class IoGem::IO
  attr_reader :out, :writes

  def _new(_fd, _mode)
    @out = +""
    @writes = []
  end

  def write(s)
    @writes << s
    @out << s
    s.bytesize
  end
end

ShellGem = Module.new
ShellGem.module_eval(File.read(File.join(ROOT, "main/ruby/default_app/shell/shell_io.rb")))
class FakeShell
  attr_reader :lines

  def initialize = (@lines = [])
  def append_output(s) = @lines << s
end

NL = 10.chr
self_ref = [9]
self_ref << self_ref
PUTS_CASES = [
  ["array", [[1, 2]], "1#{NL}2#{NL}"],
  ["nested", [[1, [2, [3]]]], "1#{NL}2#{NL}3#{NL}"],
  ["no argument", [], NL],
  ["several", ["a", "b#{NL}", 5], "a#{NL}b#{NL}5#{NL}"],
  ["nil and empty", [nil, ""], "#{NL}#{NL}"],
  ["empty array", [[]], NL],
  ["empty array inside", [[1, [], 2]], "1#{NL}#{NL}2#{NL}"],
  ["string with newline in array", [["x#{NL}", "y"]], "x#{NL}y#{NL}"],
  ["self reference", [self_ref], "9#{NL}" * 16 + "[...]#{NL}"],
  ["symbol", [:s], "s#{NL}"],
]

PUTS_CASES.each do |name, args, want|
  io = IoGem::IO.new(1, "w")
  check("IO#puts #{name} returns nil", io.puts(*args), nil)
  check("IO#puts #{name}", io.out, want)

  cap = ShellGem::OutputCapturer.new
  cap.puts(*args)
  check("OutputCapturer#puts #{name}", cap.get_output, want)

  sh = FakeShell.new
  so = ShellGem::ShellStdout.new(sh)
  so.puts(*args)
  so.flush
  # Every case ends in a newline, so the shell gets whole lines.
  check("ShellStdout#puts #{name}", sh.lines.map { |l| l + NL }.join, want)
end

# One write per piece: an Array is not joined into one string first.
io = IoGem::IO.new(1, "w")
io.puts(["ab", "cd"])
check("IO#puts writes pieces", io.writes, ["ab", NL, "cd", NL])

# ---- FmrbUI#bg= reaches the widgets that exist (doc/api_fixes F2) ------------
#
# lib/gems/picoruby-fmrb-app/mrblib/fmrb-ui.rb with a stand-in for the theme
# constants, the app and its gfx (nothing is drawn here).

module FmrbConst
  THEME_WINDOW_BG = 0xFF
  THEME_TEXT = 0x00
  THEME_TEXT_LIGHT = 0xFF
  THEME_HIGHLIGHT = 0x1C
  THEME_BORDER = 0x49
  THEME_BUTTON = 0x92
end

module Log
  def self.warn(_m) = nil
end

class StubGfx
  def text_width(s, _f = nil) = s.to_s.size * 6
  def current_text_size = 1
  def fit_text(s, *_a) = s
  def method_missing(*_a) = nil
  def respond_to_missing?(*_a) = true
end

class StubApp
  def gfx = (@gfx ||= StubGfx.new)
  def user_area_width = 320
  def user_area_height = 200
  def attach_ui(_ui) = nil
end

load File.join(ROOT, "lib/gems/picoruby-fmrb-app/mrblib/fmrb-ui.rb")

ui = FmrbUI.new(StubApp.new)
lab = ui.label(:lab, 0, 0, 60, 10, "x")
own = ui.label(:own, 0, 20, 60, 10, "y")
own.bg = 0x03
sb = ui.scrollbar(:sb, 300, 0, 10, 100, 50, 10)
hid = ui.label(:hid, 0, 40, 60, 10, "z")
ui.set_visible(:hid, false)
ui.flush
check("bg: before", [lab.bg, own.bg, sb.bg, hid.bg], [0xFF, 0x03, 0xFF, 0xFF])
check("bg: clean after flush", [lab.dirty, own.dirty, sb.dirty, hid.dirty], [false, false, false, false])
ui.bg = 0xE0
check("bg: copied ones follow, own one stays", [ui.bg, lab.bg, own.bg, sb.bg, hid.bg], [0xE0, 0xE0, 0x03, 0xE0, 0xE0])
check("bg: shown ones marked for the next flush", [lab.dirty, own.dirty, sb.dirty, hid.dirty], [true, false, true, false])
ui.flush
later = ui.button(:later, 0, 60, 60, 12, "b")
check("bg: a widget made afterwards", later.bg, 0xE0)
# A container with its own colour from the start behaves the same.
ui2 = FmrbUI.new(StubApp.new, bg: 0x10)
w2 = ui2.stepper(:st, 0, 0, 80, 12, 1, 0, 9)
ui2.bg = 0x20
check("bg: from a bg: container", w2.bg, 0x20)
# A restore (FmrbUI#_checkpoint_ground) still moves only the theme's default.
ui3 = FmrbUI.new(StubApp.new)
t3 = ui3.label(:t, 0, 0, 60, 10, "t")
o3 = ui3.label(:o, 0, 20, 60, 10, "o")
o3.bg = 0x03
t3.take_ground(0x55, true) # as if written under another theme
ui3._checkpoint_ground
check("bg: restore follows the theme only for the default", [t3.bg, o3.bg], [0xFF, 0x03])
c3 = FmrbUI.new(StubApp.new, bg: 0x10)
l3 = c3.label(:l, 0, 0, 60, 10, "l")
c3._checkpoint_ground
check("bg: restore keeps a container's chosen colour", l3.bg, 0x10)

# ---- Two MIDI players in one app (doc/api_fixes F3) --------------------------
#
# The whole of fmrb-midi.rb this time (ApuTransport), over stand-ins for the
# audio object and for the C queue. The queue keeps what fmrb_midi_sched.c
# keeps per command for this question: the player (owner) it was pushed
# with. Everything here runs as one app, so the app id does not come in.

class FmrbAudio
  def initialize(_app = nil) = nil
  def note_on(*_a) = 0
  def note_off(*_a) = 0
end

class QueueStandIn
  attr_reader :cmds
  attr_accessor :now_us

  def initialize
    @cmds = []
    @now_us = 0
  end

  def _push_apu_note(due, ch, _freq, _vol, _duty, _sweep, owner = 0) = push(due, :on, ch, owner)
  def _push_apu_off(due, ch, owner = 0) = push(due, :off, ch, owner)
  def _push_serial(due, _len, b1, _b2, _b3, owner = 0) = push(due, :serial, b1, owner)

  def push(due, kind, ch, owner)
    @cmds << [due, kind, ch, owner]
    true
  end

  # No owner: the whole app (what an app's exit does). An owner: that
  # player's only.
  def _clear(owner = nil)
    @cmds = owner.nil? ? [] : @cmds.reject { |c| c[3] == owner }
    nil
  end

  def owners = @cmds.map { |c| c[3] }.uniq.sort
  # No owner: the whole queue. An owner: that player's (fmrb_midi_sched.c
  # counts this app's commands with that owner).
  def _depth(owner = nil) = owner.nil? ? @cmds.size : @cmds.count { |c| c[3] == owner }

  # The timer: everything due by now has been sent.
  def fire!(now_us)
    @now_us = now_us
    @cmds.reject! { |c| c[0] <= now_us }
  end

  def _free = 127 - @cmds.size
  def _now_us = @now_us
  def _reset_stats = nil
  def _stats = {}
end

saved_verbose = $VERBOSE
$VERBOSE = nil # the constants defined at the top of this file come again
load File.join(ROOT, "lib/gems/picoruby-fmrb-midi/mrblib/fmrb-midi.rb")
$VERBOSE = saved_verbose

q = QueueStandIn.new
FmrbMidi.instance_variable_set(:@scheduler, q)
FmrbMidi.instance_variable_set(:@scheduler_checked, true)

def mml_player(tune, channel)
  pl = FmrbMidi::MmlPlayer.new(FmrbMidi.device(nil))
  pl.load_string(tune, channel: channel)
  pl.start
  pl
end

TUNE = "o4 l16 cdefgab>cdefgab>c"
a = mml_player(TUNE, 0)
b = mml_player(TUNE, 1)
a.tick
b.tick
oa = a.instance_variable_get(:@owner)
ob = b.instance_variable_get(:@owner)
check("midi: two players, two owners", [oa.is_a?(Integer), oa != ob, q.owners], [true, true, [oa, ob].sort])
a.stop
check("midi: stopping one keeps the other's queue", [q.owners, b.playing?], [[ob], true])
b.stop
check("midi: stopping the other empties it", q.cmds, [])

# One player alone: stopping it drops everything it queued, as before.
solo = mml_player(TUNE, 0)
solo.tick
before = q._depth
solo.pause
check("midi: one player, pause drops all of it", [before > 0, q._depth], [true, 0])

# The same between an SmfPlayer and an MmlPlayer, and through a device switch.
smf_path = File.join(ROOT, "tools/midi/test/scale.mid")
s1 = FmrbMidi::SmfPlayer.new(FmrbMidi.device(nil))
check("midi: smf loads", s1.load(smf_path), true)
s1.start
s1.tick
m1 = mml_player(TUNE, 1)
m1.tick
os = s1.instance_variable_get(:@owner)
om = m1.instance_variable_get(:@owner)
check("midi: smf and mml queued", q.owners, [os, om].sort)
s1.device = FmrbMidi.device(nil)
check("midi: smf device switch keeps the mml's", q.owners, [om])
m1.stop
check("midi: mml stop", q.cmds, [])
# What an app's exit does in C (no owner): everything of the app.
s1.tick
mml_player(TUNE, 2).tick
q._clear
check("midi: clearing the app takes all", q.cmds, [])

# playing? of one player ends when its own commands have played, while
# another player still has some queued (doc/api_fixes F4, C3).
q.now_us = 0
short = mml_player("o4 l16 c d", 0)
long = mml_player("o3 l16 " + "cdefgab" * 6, 1)
short.tick
long.tick
osh = short.instance_variable_get(:@owner)
check("midi: both queued", q.owners.include?(osh) && q.owners.size == 2, true)
q.fire!(400_000) # the short one's two notes are over, the long one goes on
long.tick
short.tick
check("midi: the long one still has commands", q._depth > 0 && q._depth(osh) == 0, true)
check("midi: the short one has ended though the other plays", [short.playing?, short.finished?, long.playing?],
      [false, true, true])
long.stop
# And one player alone still waits for its own queue to play out.
q.now_us = 0
solo2 = mml_player("o4 l16 c d", 0)
solo2.tick
solo2.tick
check("midi: alone, still playing while its notes are queued", solo2.playing?, true)
q.fire!(400_000)
solo2.tick
check("midi: alone, over once they played", solo2.playing?, false)

# The same for an SmfPlayer: one quarter note (480 ticks, 120 bpm = 0.5 s)
# beside a long MmlPlayer.
def smf_bytes(track)
  hdr = "MThd" + [6, 0, 1, 480].pack("Nnnn")
  hdr + "MTrk" + [track.bytesize].pack("N") + track
end
one_note = [0x00, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00].pack("C*")
q.now_us = 0
sm = FmrbMidi::SmfPlayer.new(FmrbMidi.device(nil))
check("midi: smf one note loads", sm.load_string(smf_bytes(one_note)), true)
sm.start
long2 = mml_player("o3 l16 " + "cdefgab" * 12, 1)
sm.tick
long2.tick
q.fire!(700_000)
long2.tick
sm.tick       # decodes the note off (due at 0.5 s), late
q.fire!(700_000)
long2.tick
sm.tick
osm = sm.instance_variable_get(:@owner)
check("midi: smf ends while the mml plays", [q._depth(osm), q._depth > 0, sm.playing?, long2.playing?],
      [0, true, false, true])
long2.stop

FmrbMidi.instance_variable_set(:@scheduler, nil)
FmrbMidi.instance_variable_set(:@scheduler_checked, false)

# ---- The pin table's GPIO rule, per board (doc/api_fixes F4, C1) -----------
#
# test/api_fixes/pin_ledger/pin_ledger_test.c over the ESP32 pin manager,
# compiled for each board with its real fmrb_pin_assign.h. The buttons are the
# only system pins an app may read as an input; the rest refuse GPIO.

require "tmpdir"
require "open3"

PIN_BOARDS = {
  # board => [chip define, the buttons (input only), the other system pins]
  "NARYAV3" => [nil, [6, 7, 8],
                [0, 1, 3, 4, 5, 9, 10, 11, 12, 13, 15, 16, 17, 18, 19, 20, 35, 36, 37, 38, 39, 45, 46]],
  "TAB5" => ["CONFIG_IDF_TARGET_ESP32P4", [], [24, 25]],
  "NARYAV4" => ["CONFIG_IDF_TARGET_ESP32P4", [], [35, 37, 38]],
  "ATOMS3R" => [nil, [41], [14, 15, 19, 20, 21, 35, 36, 37, 42, 46, 48]],
  "STICKS3" => [nil, [11, 12], [0, 19, 20, 21, 35, 36, 37, 39, 40, 41, 45]],
}

# The pins GPIO must leave alone (doc/api_fixes F6), and pins that must stay
# out of that table: the buttons and the pins left to apps (Grove, headers).
PIN_RESERVED = {
  # board => [the table, representative pins it must hold, pins it must not]
  "NARYAV3" => [[0, 1, 3, 4, 5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 43, 44, 45, 46],
                [14, 21, 11, 15, 29, 30, 43, 44], [6, 7, 8, 47, 48]],
  "TAB5" => [[0, 1, 8, 9, 10, 11, 12, 13, 15, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
              39, 40, 41, 42, 43, 44, 50],
             [39, 11, 26, 31, 50], [53, 54]],
  "NARYAV4" => [[7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 28, 29, 30, 31, 34, 35,
                 37, 38, 39, 40, 41, 42, 43, 44, 49, 50, 51, 52, 53, 54],
                [39, 14, 11, 53, 54, 28, 7], [48]],
  "ATOMS3R" => [[0, 14, 15, 19, 20, 21, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 42, 45, 46, 47, 48],
                [0, 45, 47, 29, 30], [41, 1, 2, 5, 6, 7, 8, 38, 39]],
  "STICKS3" => [[0, 14, 15, 16, 17, 18, 19, 20, 21, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
                 38, 39, 40, 41, 42, 45, 46, 47, 48],
                [47, 48, 38, 16, 42, 29, 30], [11, 12, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 43, 44]],
}
pl = File.join(ROOT, "test/api_fixes/pin_ledger")
Dir.mktmpdir("pin_ledger") do |tmp|
  PIN_BOARDS.each do |board, (chip, inputs, system)|
    exe = File.join(tmp, board)
    cmd = ["gcc", "-std=gnu11", "-Wall", "-Wno-unused-variable", "-Werror"]
    cmd << "-DFMRB_HW_#{board}" unless board == "NARYAV3" # the default branch
    cmd << "-D#{chip}" if chip
    cmd += ["-I#{pl}/stub", "-I#{ROOT}/components/fmrb_hal", "-I#{ROOT}/components/fmrb_common/include",
            "-o", exe, "#{pl}/pin_ledger_test.c",
            "#{ROOT}/components/fmrb_hal/platform/esp32/fmrb_hal_pin_manager_esp32.c"]
    out, st = Open3.capture2e(*cmd)
    check("pin ledger #{board}: builds", [st.success?, out], [true, ""])
    next unless st.success?

    out, st = Open3.capture2e(exe)
    nums = ->(label) { (out[/^#{label}:(.*)$/, 1] || "").split.map(&:to_i) }
    check("pin ledger #{board}: verdicts", [st.success?, out[/^FAIL.*$/]], [true, nil])
    check("pin ledger #{board}: buttons (input only)", nums.call("inputs"), inputs)
    check("pin ledger #{board}: other system pins", nums.call("system"), system)
    table, held, left = PIN_RESERVED.fetch(board)
    reserved = nums.call("reserved")
    check("pin ledger #{board}: pins GPIO leaves alone", reserved, table)
    check("pin ledger #{board}: the table holds the system pins", (system - reserved), [])
    check("pin ledger #{board}: representative pins in the table", held - reserved, [])
    check("pin ledger #{board}: buttons and app pins out of the table", left & reserved, [])
    check("pin ledger #{board}: a free reserved pin was tried with I2C",
          nums.call("reserved_free").first.to_i >= 0, true)
  end
end

if $failures.zero?
  puts "api_fixes: #{$count} checks passed"
else
  puts "api_fixes: #{$failures} of #{$count} checks failed"
  exit 1
end

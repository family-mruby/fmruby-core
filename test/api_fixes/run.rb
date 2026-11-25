#!/usr/bin/env ruby
# Host tests for the API fixes found while writing sig/ (doc/api_fixes F1).
# Each runs the real Ruby file under host Ruby with the little it needs
# stubbed:
#
#   - File.path (class method) returns its argument
#     (lib/gems/picoruby-fmrb-filesystem/mrblib/file.rb, loaded into a module
#     so it does not replace the host's own File)
#   - JSON::Digger#dig raises past the end of an Array, and JSON.parse makes
#     \uXXXX a UTF-8 character (lib/upstream/patch/picoruby-json/mrblib/json.rb)
#   - MIDI::Device#pitch_bend works without Integer#clamp
#     (lib/gems/picoruby-fmrb-midi/mrblib/fmrb-midi.rb over picoruby-midi)
#   - SmfPlayer stops a note at the pitch it started at when transpose
#     changes in between (fmrb-smf.rb)
#   - MmlPlayer#load_text clears "no notes" once a later part has notes
#     (fmrb-mml.rb)
#
# The C halves (File.open modes, Kernel.send_message) are checked on the sim
# (doc/api_fixes/report/f1.md).
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

# ---- MIDI --------------------------------------------------------------------

module FmrbMidi
  CC_DUTY = 70
  CC_VOLUME = 7
  CH_PULSE1 = 0
  CH_PULSE2 = 1
  CH_TRIANGLE = 2
  CH_NOISE = 3
  def self.scheduler = nil
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

if $failures.zero?
  puts "api_fixes: #{$count} checks passed"
else
  puts "api_fixes: #{$failures} of #{$count} checks failed"
  exit 1
end

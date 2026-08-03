#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Host-side test for the serial (UART) MIDI transport.
#
# The wire format lives in Ruby precisely so it can be checked here: what
# bytes each USB-MIDI packet turns into, how many of them, and that the
# stream stays self-describing (no running status). The C side is stubbed
# out, so no serial port, firmware build or simulation is involved.
#
# The last section plays the same song into the APU transport and the serial
# transport and compares them, which is the point of the player not knowing
# what it is driving.
#
# Usage: ruby tool/midi/test/serial_midi_test.rb

ROOT = File.expand_path("../../..", __dir__)
FIXTURES = __dir__

# --- stubs ---------------------------------------------------------------

class FmrbAudio
  Call = Struct.new(:at, :kind, :channel, :freq, :volume)

  attr_reader :calls

  def initialize
    @calls = []
  end

  def note_on(channel, freq, volume = 10, _duty = 2, _sweep = 0)
    @calls << Call.new(Machine.board_millis, :on, channel, freq, volume)
    0
  end

  def note_off(channel)
    @calls << Call.new(Machine.board_millis, :off, channel, nil, nil)
    0
  end
end

module Machine
  def self.board_millis
    @now ||= 0
  end

  def self.set(ms)
    @now = ms
  end

  def self.advance(ms)
    @now = board_millis + ms
  end
end

require File.join(ROOT, "lib/add/picoruby-midi/mrblib/midi_constants.rb")
require File.join(ROOT, "lib/add/picoruby-midi/mrblib/midi_device.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-serial-midi.rb")
require File.join(ROOT, "lib/add/picoruby-fmrb-midi/mrblib/fmrb-smf.rb")

# Stands in for the C class that owns the UART. Records every byte with the
# time it was written, which is what the simulation's FIFO monitor sees.
module FmrbMidi
  class SerialPort
    class << self
      attr_reader :writes

      def reset!
        @writes = []
        @open = false
      end

      def _open(_path = nil, _uart = -1, _tx = -1, _rx = -1, _baud = 31250)
        @writes ||= []
        @open = true
      end

      def _close
        @open = false
        nil
      end

      def _open?
        @open ? true : false
      end

      def _write(bytes)
        @writes ||= []
        @writes << [Machine.board_millis, bytes.dup]
        bytes.length
      end

      # All bytes written so far, flattened.
      def stream
        (@writes || []).map { |_, b| b }.join
      end

      def hex
        stream.bytes.map { |b| format("%02X", b) }.join(" ")
      end
    end
  end
end

# --- harness -------------------------------------------------------------

$checks = 0
$failures = []

def check(label)
  $checks += 1
  ok, detail = yield
  if ok
    puts "  ok   #{label}#{detail ? " (#{detail})" : ''}"
  else
    puts "  FAIL #{label}: #{detail}"
    $failures << label
  end
rescue StandardError => e
  puts "  FAIL #{label}: #{e.class}: #{e.message}"
  puts "       #{e.backtrace.first}"
  $failures << label
end

def new_serial_device
  FmrbMidi::SerialPort.reset!
  transport = FmrbMidi::SerialTransport.new
  [MIDI::Device.new(transport), transport]
end

# --- wire format ---------------------------------------------------------

puts "wire format"
device, = new_serial_device
check("note on is three bytes") do
  device.note_on(60, 100, channel: 0)
  [FmrbMidi::SerialPort.hex == "90 3C 64", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("note off is three bytes") do
  device.note_off(60, 0, channel: 0)
  [FmrbMidi::SerialPort.hex == "80 3C 00", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("the channel goes in the status byte") do
  device.note_on(60, 100, channel: 3)
  [FmrbMidi::SerialPort.hex == "93 3C 64", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("program change is two bytes, not three") do
  device.program_change(5, channel: 0)
  [FmrbMidi::SerialPort.hex == "C0 05", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("channel pressure is two bytes") do
  device.channel_pressure(64, channel: 0)
  [FmrbMidi::SerialPort.hex == "D0 40", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("control change is three bytes") do
  device.control_change(7, 100, channel: 0)
  [FmrbMidi::SerialPort.hex == "B0 07 64", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("a realtime message is one byte") do
  device.send_clock
  [FmrbMidi::SerialPort.hex == "F8", FmrbMidi::SerialPort.hex]
end

device, = new_serial_device
check("SysEx goes out whole") do
  device.send_sysex([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7])
  [FmrbMidi::SerialPort.hex == "F0 7E 7F 09 01 F7", FmrbMidi::SerialPort.hex]
end

_, transport = new_serial_device
check("a CIN that carries no data is refused") do
  # 0x0 and 0x1 are reserved; sending them would desynchronize the receiver.
  [transport.send_packet(0, 0x0, 0x90, 60, 100) == -1 && FmrbMidi::SerialPort.stream.empty?,
   FmrbMidi::SerialPort.hex]
end

puts "running status"
device, = new_serial_device
check("repeated notes still carry their status byte") do
  device.note_on(60, 100, channel: 0)
  device.note_on(62, 100, channel: 0)
  device.note_on(64, 100, channel: 0)
  # With running status this would be "90 3C 64 3E 64 40 64".
  want = "90 3C 64 90 3E 64 90 40 64"
  [FmrbMidi::SerialPort.hex == want, FmrbMidi::SerialPort.hex]
end

puts "transport interface"
device, transport = new_serial_device
check("it reports itself as a serial transport") do
  [transport.transport_id == 2, transport.transport_id.to_s]
end
check("it is send only") do
  [transport.read_available == "" && transport.bytes_available == 0, "ok"]
end
check("device.info comes from the transport") do
  info = device.info
  [info[:direction] == :out, info.inspect]
end
check("all_off silences all sixteen channels") do
  FmrbMidi::SerialPort.reset!
  FmrbMidi::SerialPort._open
  transport.all_off
  bytes = FmrbMidi::SerialPort.stream.bytes
  # Two control changes per channel, three bytes each.
  [bytes.size == 16 * 2 * 3, "#{bytes.size} bytes"]
end

puts "SAM2695 layer"
FmrbMidi::SerialPort.reset!
sam = FmrbMidi::Sam2695.new
check("it resets the chip on the way up") do
  # GM System On, then the all-notes-off sweep.
  [FmrbMidi::SerialPort.hex.start_with?("F0 7E 7F 09 01 F7"), FmrbMidi::SerialPort.hex[0, 40]]
end
check("it says it is a General MIDI device") do
  info = sam.device_info
  [info[:general_midi] == true && info[:name] == "SAM2695", info.inspect]
end
check("FmrbMidi.sam2695_device hands back a MIDI::Device") do
  FmrbMidi::SerialPort.reset!
  d = FmrbMidi.sam2695_device
  [d.is_a?(MIDI::Device), d.class.to_s]
end
check("it returns nil when the port will not open") do
  FmrbMidi::SerialPort.reset!
  # Simulate a port that refuses to open (no FIFO, or pins already taken).
  class << FmrbMidi::SerialPort
    alias_method :_open_real, :_open
    alias_method :_open_real?, :_open?
    def _open(*_args) = false
    def _open? = false
  end
  d = FmrbMidi.sam2695_device
  class << FmrbMidi::SerialPort
    alias_method :_open, :_open_real
    alias_method :_open?, :_open_real?
  end
  [d.nil?, d.inspect]
end

# --- the same song on both outputs ---------------------------------------

puts "trigger on the serial transport"
# Earlier sections left their transports in the registry; MIDI._trigger walks
# it, so start from a clean one to measure just this transport.
FmrbMidi.transports.clear
FmrbMidi::SerialPort.reset!
Machine.set(0)
serial = FmrbMidi::SerialTransport.new
dev = MIDI::Device.new(serial)
check("trigger sends the note at once") do
  FmrbMidi::SerialPort.reset!
  FmrbMidi::SerialPort._open
  dev.trigger(38, 110, duration: 90, channel: 9)
  # Note on, channel 10 in one-based counting.
  [FmrbMidi::SerialPort.hex == "99 26 6E", FmrbMidi::SerialPort.hex]
end
check("the note off follows once pumped") do
  FmrbMidi::SerialPort.reset!
  FmrbMidi::SerialPort._open
  Machine.advance(100)
  FmrbMidi.tick
  [FmrbMidi::SerialPort.hex == "89 26 00", FmrbMidi::SerialPort.hex]
end

puts "the same song through both transports"

def play_song(player)
  while player.playing?
    wait = player.tick
    break if wait.nil?

    Machine.advance(wait > 0 ? wait : 1)
  end
end

# APU path
Machine.set(0)
audio = FmrbAudio.new
apu_device = MIDI::Device.new(FmrbMidi::ApuTransport.new(audio))
apu_player = FmrbMidi::SmfPlayer.new(apu_device)
apu_player.load(File.join(FIXTURES, "scale.mid"))
apu_player.start
play_song(apu_player)
apu_notes = audio.calls.select { |c| c.kind == :on }

# Serial path, same file, same clock
Machine.set(0)
FmrbMidi::SerialPort.reset!
FmrbMidi::SerialPort._open
serial_device = MIDI::Device.new(FmrbMidi::SerialTransport.new)
serial_player = FmrbMidi::SmfPlayer.new(serial_device)
serial_player.load(File.join(FIXTURES, "scale.mid"))
serial_player.start
play_song(serial_player)

serial_notes = FmrbMidi::SerialPort.writes.filter_map do |at, bytes|
  b = bytes.bytes
  next unless (b[0] & 0xF0) == 0x90 && b[2] > 0

  [at, b[1]]
end

check("both play the same number of notes") do
  [serial_notes.size == apu_notes.size && serial_notes.size == 8,
   "apu=#{apu_notes.size} serial=#{serial_notes.size}"]
end

check("the serial side sends the note numbers from the file") do
  want = [60, 62, 64, 65, 67, 69, 71, 72]
  [serial_notes.map { |_, note| note } == want, serial_notes.map { |_, n| n }.inspect]
end

check("the APU side plays the frequencies those notes map to") do
  want = [60, 62, 64, 65, 67, 69, 71, 72].map { |n| FmrbMidi::PULSE_FREQ[n] }
  [apu_notes.map(&:freq) == want, apu_notes.map(&:freq).inspect]
end

check("the two outputs are in step to the millisecond") do
  apu_times = apu_notes.map(&:at)
  serial_times = serial_notes.map { |at, _| at }
  [apu_times == serial_times, "apu #{apu_times.inspect} serial #{serial_times.inspect}"]
end

check("note spacing is the 500 ms the file asks for") do
  times = serial_notes.map { |at, _| at }
  gaps = []
  i = 1
  while i < times.size
    gaps << (times[i] - times[i - 1])
    i += 1
  end
  [gaps.all? { |g| (g - 500).abs <= 1 }, gaps.inspect]
end

puts "tempo_scale through the serial path"
Machine.set(0)
FmrbMidi::SerialPort.reset!
FmrbMidi::SerialPort._open
player = FmrbMidi::SmfPlayer.new(MIDI::Device.new(FmrbMidi::SerialTransport.new))
player.load(File.join(FIXTURES, "scale.mid"))
player.tempo_scale = 2.0
player.start
play_song(player)
check("at 2.0 the bytes come out twice as fast") do
  times = FmrbMidi::SerialPort.writes.filter_map do |at, bytes|
    b = bytes.bytes
    (b[0] & 0xF0) == 0x90 && b[2] > 0 ? at : nil
  end
  gaps = []
  i = 1
  while i < times.size
    gaps << (times[i] - times[i - 1])
    i += 1
  end
  [gaps.all? { |g| (g - 250).abs <= 1 }, gaps.inspect]
end

puts
puts "#{$checks} checks, #{$failures.size} failed"
exit($failures.empty? ? 0 : 1)

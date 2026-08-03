#!/usr/bin/env ruby
# frozen_string_literal: true
#
# fmsq_dump - decode an FMSQ file into readable APU events.
#
# Works on any FMSQ file (smf2fmsq output, nsf2fmsq output, the bytes BASIC's
# PLAY hands to the audio side). Register writes are shown with the frame they
# land on and, where it makes sense, the pitch or volume they encode.
#
# Usage:
#   ruby fmsq_dump.rb file.fmsq [--notes] [--raw]

require "optparse"

module FmsqDump
  CPU_CLOCK = 1_789_773

  REG_NAMES = {
    0x00 => "PULSE1_VOL", 0x01 => "PULSE1_SWEEP", 0x02 => "PULSE1_LO", 0x03 => "PULSE1_HI",
    0x04 => "PULSE2_VOL", 0x05 => "PULSE2_SWEEP", 0x06 => "PULSE2_LO", 0x07 => "PULSE2_HI",
    0x08 => "TRI_LINEAR", 0x0A => "TRI_LO", 0x0B => "TRI_HI",
    0x0C => "NOISE_VOL", 0x0E => "NOISE_PERIOD", 0x0F => "NOISE_LEN",
    0x10 => "DMC_FREQ", 0x11 => "DMC_RAW", 0x12 => "DMC_START", 0x13 => "DMC_LEN",
    0x15 => "STATUS"
  }.freeze

  NOTE_NAMES = %w[C C# D D# E F F# G G# A A# B].freeze

  class Error < StandardError; end

  Header = Struct.new(:version, :flags, :frame_count, :data_size, :loop_offset)
  Write = Struct.new(:frame, :offset, :value, keyword_init: true)

  # A note as reconstructed from the register writes.
  Note = Struct.new(:frame, :voice, :frequency, :volume, :duty, :period, :short,
                    keyword_init: true) do
    def name
      return "-" if frequency.nil? || frequency <= 0

      n = (69 + (12 * Math.log2(frequency / 440.0))).round
      "#{NOTE_NAMES[n % 12]}#{(n / 12) - 1}"
    end
  end

  module_function

  def read(path)
    bytes = File.binread(path)
    raise Error, "not an FMSQ file" unless bytes[0, 4] == "FMSQ"

    version, flags, frame_count, data_size, loop_offset = bytes.byteslice(4, 8).unpack("CCvvv")
    header = Header.new(version, flags, frame_count, data_size, loop_offset)
    [header, bytes.byteslice(12, data_size)]
  end

  # Decode the command stream into register writes tagged with their frame.
  # Loops are not followed (one pass through the data).
  def writes(data)
    out = []
    frame = 0
    pc = 0
    while pc < data.bytesize
      cmd = data.getbyte(pc)
      pc += 1
      if (cmd & 0x80).zero?
        frame += (cmd & 0x7F) + 1
      elsif cmd == 0xFE
        break
      elsif cmd == 0xFF
        pc += 2
        break
      elsif (cmd & 0xE0) == 0xC0
        value = data.getbyte(pc)
        pc += 1
        out << Write.new(frame: frame, offset: cmd & 0x1F, value: value)
      elsif (cmd & 0xC0) == 0x80
        # Channel command (NOTE_ON / NOTE_OFF / PARAM): payload length depends
        # on the channel, and smf2fmsq never emits these, so stop rather than
        # guess and desynchronize.
        raise Error, format("channel command 0x%02X at byte %d is not decoded here", cmd, pc - 1)
      elsif cmd >= 0xE0 && cmd <= 0xE2
        pc += cmd == 0xE0 ? 3 : (cmd == 0xE2 ? 1 : 0)
      else
        raise Error, format("unknown command 0x%02X at byte %d", cmd, pc - 1)
      end
    end
    out
  end

  # Reconstruct note events. A pulse or triangle note starts when the timer
  # high byte is written; a noise hit starts when the length counter is loaded.
  def notes(writes)
    state = { timer_lo: {}, vol: {}, period: nil, short: false }
    out = []
    writes.each do |w|
      case w.offset
      when 0x02, 0x06, 0x0A then state[:timer_lo][w.offset] = w.value
      when 0x00 then state[:vol][:pulse1] = w.value
      when 0x04 then state[:vol][:pulse2] = w.value
      when 0x0C then state[:vol][:noise] = w.value
      when 0x0E
        state[:period] = w.value & 0x0F
        state[:short] = (w.value & 0x80) != 0
      end

      case w.offset
      when 0x03, 0x07
        pulse = w.offset == 0x03 ? :pulse1 : :pulse2
        lo = state[:timer_lo][w.offset - 1] || 0
        timer = ((w.value & 0x07) << 8) | lo
        vol = state[:vol][pulse] || 0
        out << Note.new(frame: w.frame, voice: pulse,
                        frequency: timer_frequency(timer, 16),
                        volume: vol & 0x0F, duty: (vol >> 6) & 0x03)
      when 0x0B
        lo = state[:timer_lo][0x0A] || 0
        timer = ((w.value & 0x07) << 8) | lo
        out << Note.new(frame: w.frame, voice: :triangle,
                        frequency: timer_frequency(timer, 32))
      when 0x0F
        out << Note.new(frame: w.frame, voice: :noise,
                        volume: (state[:vol][:noise] || 0) & 0x0F,
                        period: state[:period], short: state[:short])
      end
    end
    out
  end

  def timer_frequency(timer, divider)
    return nil if timer.zero?

    CPU_CLOCK / (divider.to_f * (timer + 1))
  end

  def run(argv)
    options = { notes: false, raw: false }
    parser = OptionParser.new do |o|
      o.banner = "Usage: fmsq_dump.rb FILE.fmsq [options]"
      o.on("--notes", "show reconstructed notes only") { options[:notes] = true }
      o.on("--raw", "show every register write") { options[:raw] = true }
      o.on("-h", "--help") do
        puts o
        return 0
      end
    end
    parser.parse!(argv)

    path = argv.shift
    if path.nil?
      warn parser.help
      return 1
    end

    header, data = read(path)
    w = writes(data)
    puts "#{path}: version=#{header.version} frames=#{header.frame_count} " \
         "(#{format('%.2f', header.frame_count / 60.0)} s) data=#{header.data_size} B " \
         "loop=#{header.loop_offset} writes=#{w.size}"

    options[:raw] = true unless options[:notes]
    if options[:raw]
      puts "  frame  reg            value"
      w.each do |x|
        printf("  %5d  %-14s 0x%02X\n", x.frame, REG_NAMES.fetch(x.offset, format("$40%02X", x.offset)),
               x.value)
      end
    end

    if options[:notes]
      puts "  frame  voice     freq(Hz)  note   vol  extra"
      notes(w).each do |n|
        extra = n.voice == :noise ? "period=#{n.period}#{n.short ? ' short' : ''}" : "duty=#{n.duty}"
        printf("  %5d  %-8s %9s  %-5s %3s  %s\n", n.frame, n.voice,
               n.frequency ? format("%.2f", n.frequency) : "-", n.name,
               n.volume || "-", extra)
      end
    end
    0
  rescue Error, Errno::ENOENT => e
    warn "fmsq_dump: #{e.message}"
    1
  end
end

exit(FmsqDump.run(ARGV)) if $PROGRAM_NAME == __FILE__

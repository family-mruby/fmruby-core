# Family mruby OS - MIDI over a serial port
#
# Sends real MIDI bytes at 31250 baud, so anything that speaks MIDI DIN can
# be driven from the same app code that drives the built-in APU:
#
#   device = FmrbMidi.sam2695_device      # M5Stack Unit MIDI on the GROVE port
#   device.program_change(0)              # GM piano
#   device.note_on(60, 100)
#
# The UART itself belongs to the system side (see include/fmrb_midi_serial.h):
# apps get a MIDI::Device transport and nothing else, so there is no general
# "UART from Ruby" path to fight over.
#
# Everything about the wire format lives here rather than in C, so it can be
# checked on a host without a serial port (tool/midi/test/serial_midi_test.rb).

module FmrbMidi
  # How many MIDI bytes a USB-MIDI packet carries on the wire, indexed by its
  # Code Index Number. picoruby-midi builds packets in the USB-MIDI shape
  # whatever the transport, so a serial transport has to unpack them again.
  #
  # 0 marks the CINs that carry no wire data (reserved / cable events).
  WIRE_BYTES = [
    0, # 0x0 miscellaneous, reserved
    0, # 0x1 cable event, reserved
    2, # 0x2 two-byte system common
    3, # 0x3 three-byte system common
    3, # 0x4 SysEx starts or continues
    1, # 0x5 SysEx ends with one byte, or single-byte system common
    2, # 0x6 SysEx ends with two bytes
    3, # 0x7 SysEx ends with three bytes
    3, # 0x8 note off
    3, # 0x9 note on
    3, # 0xA poly key pressure
    3, # 0xB control change
    2, # 0xC program change
    2, # 0xD channel pressure
    3, # 0xE pitch bend
    1  # 0xF single byte
  ]

  # Matches MIDI_TRANSPORT_ID_SERIAL in picoruby-midi's transport interface,
  # and what Midori's UART_MIDI reports.
  TRANSPORT_ID_SERIAL = 2

  DEFAULT_BAUD = 31250

  # GM System On. Puts a General MIDI receiver into a known state, which
  # matters because the previous program change and controller values
  # survive a reset of the sending side.
  GM_SYSTEM_ON = "\xF0\x7E\x7F\x09\x01\xF7"

  class SerialTransport
    # Gives this transport MIDI::Device#trigger, the same way ApuTransport
    # gets it: the app pumps the scheduled note offs.
    include NoteScheduler

    attr_reader :baud, :path, :uart_num, :tx_pin, :rx_pin

    # Options are all optional; the C side fills in what this board uses
    # (a FIFO in the simulation, GROVE port 2 on the Retro board).
    #
    # @param path [String, nil] device path, simulation only
    # @param uart [Integer, nil] UART unit, hardware only
    # @param tx [Integer, nil] TX pin, hardware only
    # @param rx [Integer] RX pin, -1 for output only
    # @param baud [Integer] 31250 unless a device needs otherwise
    def initialize(path: nil, uart: nil, tx: nil, rx: -1, baud: ::FmrbMidi::DEFAULT_BAUD)
      @path = path
      @uart_num = uart
      @tx_pin = tx
      @rx_pin = rx
      @baud = baud
      @pending = [] # scheduled note offs, see NoteScheduler
      # One buffer per wire length, reused. Building "\x00" * length per
      # message is one String per MIDI event, and playback has to leave the
      # collector alone (doc/midi/report/p6.md). _write copies the bytes
      # before returning, so the same buffer can serve the next message.
      # Built with String#* rather than written as literals so they are
      # certainly mutable strings of this VM's own making.
      @wire = [nil, "\x00" * 1, "\x00" * 2, "\x00" * 3]
      # An instant with a due time hands its bytes to the C queue instead of
      # writing them now, so the beat comes from a timer rather than from
      # whenever the app's task woke up (doc/midi/report/p7_6.md).
      @instant_us = nil
      @sched = FmrbMidi.scheduler
      @opened = ::FmrbMidi::SerialPort._open(path, uart || -1, tx || -1, rx, baud)
      FmrbMidi.register(self) if @opened
    end

    # --- transport interface expected by MIDI::Device --------------------

    # Turn a USB-MIDI packet back into the bytes a MIDI cable carries.
    #
    # Running status (leaving the status byte out when it repeats) is not
    # used. It would save a byte per note, but receivers differ in how well
    # they handle it, and at 31250 baud a busy piece needs well under a
    # tenth of the line: about 30 bytes for ten notes in a 20 ms window
    # against the 60 bytes the line carries in that time. Not worth the
    # class of bug it invites.
    def send_packet(_cable, cin, b1, b2, b3)
      length = ::FmrbMidi::WIRE_BYTES[cin & 0x0F]
      return -1 if length == 0

      if @instant_us
        return @sched._push_serial(@instant_us, length, b1 & 0xFF,
                                   b2 & 0xFF, b3 & 0xFF) ? length : -1
      end

      # picoruby has no Array#pack, so the buffer is built with setbyte.
      buffer = @wire[length]
      buffer.setbyte(0, b1 & 0xFF)
      buffer.setbyte(1, b2 & 0xFF) if length > 1
      buffer.setbyte(2, b3 & 0xFF) if length > 2
      ::FmrbMidi::SerialPort._write(buffer)
    end

    # Send bytes as they are (device-specific messages, SysEx).
    def send_raw(bytes)
      ::FmrbMidi::SerialPort._write(bytes)
    end

    def read_available
      ""
    end

    def bytes_available
      0
    end

    def connected?
      ::FmrbMidi::SerialPort._open?
    end

    def device_info
      { name: "Serial MIDI", baud: @baud, direction: :out }
    end

    def transport_id
      ::FmrbMidi::TRANSPORT_ID_SERIAL
    end

    # An instant, the same pair ApuTransport offers. There are no voices to
    # resolve here - a real instrument plays every note it is given - so all
    # this does is stamp the messages of one musical instant with the time
    # they are due, which is what the player needs from any output.
    def defer_voices(due_us = nil)
      @instant_us = @sched ? due_us : nil
      0
    end

    def flush_voices
      @instant_us = nil
      0
    end

    # Nothing of ours is sounding once every channel has been told to stop;
    # kept for symmetry with ApuTransport so an app can silence any device.
    def all_off
      channel = 0
      while channel < 16
        send_packet(0, 0xB, 0xB0 | channel, 123, 0) # all notes off
        send_packet(0, 0xB, 0xB0 | channel, 120, 0) # all sound off
        channel += 1
      end
      0
    end

    def close
      ::FmrbMidi::SerialPort._close
    end

    # NoteScheduler drives these two, and they are worth having on their own:
    # an app can send a note without going through MIDI::Device.
    def note_on(channel, note, velocity)
      send_packet(0, 0x9, 0x90 | (channel & 0x0F), note & 0x7F, velocity & 0x7F)
    end

    def note_off(channel, note)
      send_packet(0, 0x8, 0x80 | (channel & 0x0F), note & 0x7F, 0)
    end
  end

  # M5Stack Unit MIDI and friends: a SAM2695 on a UART. Protocol-wise this is
  # plain MIDI DIN, so the only thing worth having is the reset and a place
  # to hang chip-specific helpers later (this mirrors Midori's
  # picoruby-sam2695, which is a thin wrapper over its UART transport).
  class Sam2695 < SerialTransport
    def initialize(path: nil, uart: nil, tx: nil, rx: -1, baud: ::FmrbMidi::DEFAULT_BAUD)
      super(path: path, uart: uart, tx: tx, rx: rx, baud: baud)
      reset if connected?
    end

    # GM System On, then silence every channel: the chip keeps its state
    # across a reset of this board, so a fresh app should not inherit the
    # previous one's instruments or stuck notes.
    def reset
      send_raw(::FmrbMidi::GM_SYSTEM_ON)
      all_off
      0
    end

    def device_info
      { name: "SAM2695", baud: @baud, direction: :out, general_midi: true }
    end
  end

  class << self
    # MIDI::Device on a plain serial port, or nil when the port will not
    # open (no FIFO in the simulation, pins taken on hardware).
    def serial_device(path: nil, uart: nil, tx: nil, rx: -1, baud: ::FmrbMidi::DEFAULT_BAUD)
      transport = SerialTransport.new(path: path, uart: uart, tx: tx, rx: rx, baud: baud)
      return nil unless transport.connected?

      ::MIDI::Device.new(transport)
    end

    # MIDI::Device on a SAM2695 unit, reset and ready.
    def sam2695_device(path: nil, uart: nil, tx: nil, rx: -1, baud: ::FmrbMidi::DEFAULT_BAUD)
      transport = Sam2695.new(path: path, uart: uart, tx: tx, rx: rx, baud: baud)
      return nil unless transport.connected?

      ::MIDI::Device.new(transport)
    end
  end
end

# MIDI Device
#
# Abstract MIDI device interface supporting various transports
# (USB-MIDI, Serial MIDI, BLE-MIDI, etc.)

module MIDI
  class Device
    # @param transport [Object] Transport layer (USB_MIDI, SerialMIDI, etc.)
    #   Must implement: send_packet(cable, cin, b1, b2, b3),
    #                   read_available, bytes_available, connected?
    def initialize(transport)
      @transport = transport
      @cable = 0
    end

    attr_accessor :cable
    attr_reader :transport

    # Check if device is connected
    def connected?
      @transport.connected?
    end

    # Get device information
    def info
      @transport.device_info
    end

    # --- Channel Voice Messages ---

    # Send Note On
    # @param note [Integer] Note number (0-127)
    # @param velocity [Integer] Velocity (0-127, default: 127)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def note_on(note, velocity = 127, channel: 0)
      send_channel_message(NOTE_ON, channel, note, velocity)
    end

    # Send Note Off
    # @param note [Integer] Note number (0-127)
    # @param velocity [Integer] Velocity (0-127, default: 0)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def note_off(note, velocity = 0, channel: 0)
      send_channel_message(NOTE_OFF, channel, note, velocity)
    end

    # Send Control Change
    # @param cc [Integer] Control number (0-127)
    # @param value [Integer] Control value (0-127)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def control_change(cc, value, channel: 0)
      send_channel_message(CONTROL_CHANGE, channel, cc, value)
    end

    # Send Program Change
    # @param program [Integer] Program number (0-127)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def program_change(program, channel: 0)
      status = PROGRAM_CHANGE | (channel & 0x0F)
      @transport.send_packet(@cable, CIN_PROGRAM_CHANGE, status, program, 0)
    end

    # Send Pitch Bend
    # @param value [Integer] Pitch bend value (-8192 to 8191, center: 0)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def pitch_bend(value, channel: 0)
      # Convert to 14-bit unsigned (0-16383, center: 8192)
      v = (value + 8192).clamp(0, 16383)
      lsb = v & 0x7F
      msb = (v >> 7) & 0x7F
      status = PITCH_BEND | (channel & 0x0F)
      @transport.send_packet(@cable, CIN_PITCH_BEND, status, lsb, msb)
    end

    # Send Polyphonic Aftertouch
    # @param note [Integer] Note number (0-127)
    # @param pressure [Integer] Pressure value (0-127)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def poly_aftertouch(note, pressure, channel: 0)
      send_channel_message(POLY_AFTERTOUCH, channel, note, pressure)
    end

    # Send Channel Pressure (Aftertouch)
    # @param pressure [Integer] Pressure value (0-127)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    def channel_pressure(pressure, channel: 0)
      status = CHANNEL_PRESSURE | (channel & 0x0F)
      @transport.send_packet(@cable, CIN_CHANNEL_PRESSURE, status, pressure, 0)
    end

    # --- Trigger Method (for simultaneous multi-note) ---

    # Trigger a note with automatic note_off after duration
    #
    # Unlike note_on/note_off pair with sleep, this method returns immediately
    # and schedules the note_off in the background. This enables true
    # simultaneous triggering of multiple notes (e.g., multi-touch pads).
    #
    # @param note [Integer] Note number (0-127)
    # @param velocity [Integer] Velocity (0-127, default: 127)
    # @param duration [Integer] Duration in milliseconds before note_off (default: 100)
    # @param channel [Integer] MIDI channel (0-15, default: 0)
    # @return [Integer] 0 on success, -1 on error (scheduler full)
    #
    # @example Basic usage
    #   device.trigger(60, 100, duration: 200)
    #
    # @example Multi-touch pad (all notes start simultaneously)
    #   UI.pad(0) { device.trigger(36, 127, duration: 100) }
    #   UI.pad(1) { device.trigger(38, 127, duration: 100) }
    #
    def trigger(note, velocity = 127, duration: 100, channel: 0)
      transport_mask = _get_transport_mask
      MIDI._trigger(transport_mask, channel, note, velocity, duration)
    end

    # Trigger multiple note_on events at once with automatic note_off
    # scheduling. All note_ons are sent from C code with no Ruby overhead,
    # so a chord of 4-8 notes lands within microseconds.
    #
    # Note: only :note_on entries are processed; pair them with `duration_ms`
    # so the scheduler emits the matching note_offs. Standalone note_off
    # should still go through `note_off`.
    #
    # @param events [Array<Hash>] Array of event hashes with keys:
    #   - :type => :note_on (Symbol)
    #   - :channel => MIDI channel (0-15, Integer)
    #   - :note => note number (0-127, Integer)
    #   - :velocity => velocity (0-127, Integer)
    #   - :duration_ms => note_off delay (Integer, default 1000)
    # @return [Integer] Number of events sent
    #
    # @example Send simultaneous notes
    #   events = [
    #     {type: :note_on, channel: 0, note: 60, velocity: 100, duration_ms: 200},
    #     {type: :note_on, channel: 1, note: 64, velocity: 100, duration_ms: 200},
    #     {type: :note_on, channel: 2, note: 67, velocity: 100, duration_ms: 200}
    #   ]
    #   device.trigger_batch(events)
    #
    def trigger_batch(events)
      transport_mask = _get_transport_mask

      # Add transport to each event
      events_with_transport = []
      events.each do |event|
        event_copy = {
          type: event[:type],
          transport: transport_mask,
          channel: event[:channel],
          note: event[:note],
          velocity: event[:velocity] || 100,
          duration_ms: event[:duration_ms] || 1000
        }
        events_with_transport << event_copy
      end

      MIDI._send_batch(events_with_transport)
    end

    # Backward-compatible alias for trigger_batch. Kept so existing scripts
    # that use the old name keep working; new code should call trigger_batch.
    alias_method :send_midi_batch, :trigger_batch

    # --- Convenience Methods ---

    # Send Modulation (CC #1)
    def modulation(value, channel: 0)
      control_change(CC::MODULATION, value, channel: channel)
    end

    # Send Volume (CC #7)
    def volume(value, channel: 0)
      control_change(CC::VOLUME, value, channel: channel)
    end

    # Send Pan (CC #10)
    def pan(value, channel: 0)
      control_change(CC::PAN, value, channel: channel)
    end

    # Send Expression (CC #11)
    def expression(value, channel: 0)
      control_change(CC::EXPRESSION, value, channel: channel)
    end

    # Send Sustain Pedal (CC #64)
    def sustain(on_off, channel: 0)
      value = on_off ? 127 : 0
      control_change(CC::SUSTAIN, value, channel: channel)
    end

    # Send All Notes Off (CC #123)
    def all_notes_off(channel: 0)
      control_change(CC::ALL_NOTES_OFF, 0, channel: channel)
    end

    # Send All Sound Off (CC #120)
    def all_sound_off(channel: 0)
      control_change(CC::ALL_SOUND_OFF, 0, channel: channel)
    end

    # Reset All Controllers (CC #121)
    def reset_all_controllers(channel: 0)
      control_change(CC::RESET_ALL_CTRL, 0, channel: channel)
    end

    # --- System Realtime Messages ---

    # Send MIDI Clock
    def send_clock
      @transport.send_packet(@cable, CIN_SINGLE_BYTE, TIMING_CLOCK, 0, 0)
    end

    # Send Start
    def send_start
      @transport.send_packet(@cable, CIN_SINGLE_BYTE, START, 0, 0)
    end

    # Send Stop
    def send_stop
      @transport.send_packet(@cable, CIN_SINGLE_BYTE, STOP, 0, 0)
    end

    # Send Continue
    def send_continue
      @transport.send_packet(@cable, CIN_SINGLE_BYTE, CONTINUE, 0, 0)
    end

    # --- System Exclusive ---

    # Send SysEx message
    # @param data [Array<Integer>] SysEx data bytes
    # @param wrap [Boolean] If true, automatically add F0 and F7
    def send_sysex(data, wrap: false)
      bytes = wrap ? [SYSEX_START] + data + [SYSEX_END] : data

      return if bytes.empty?

      # Send in 3-byte chunks using USB-MIDI SysEx packets
      i = 0
      while i < bytes.length
        remaining = bytes.length - i

        if i == 0 && bytes[0] == SYSEX_START
          # SysEx start
          if remaining >= 3
            @transport.send_packet(@cable, CIN_SYSEX_START,
                                   bytes[i], bytes[i + 1], bytes[i + 2])
            i += 3
          else
            # Should not happen with valid SysEx
            break
          end
        elsif remaining == 1 && bytes[i] == SYSEX_END
          # SysEx end, 1 byte
          @transport.send_packet(@cable, CIN_SYSCOMMON_1, bytes[i], 0, 0)
          i += 1
        elsif remaining == 2 && bytes[i + 1] == SYSEX_END
          # SysEx end, 2 bytes
          @transport.send_packet(@cable, CIN_SYSEX_END_2,
                                 bytes[i], bytes[i + 1], 0)
          i += 2
        elsif remaining >= 3 && bytes[i + 2] == SYSEX_END
          # SysEx end, 3 bytes
          @transport.send_packet(@cable, CIN_SYSEX_END_3,
                                 bytes[i], bytes[i + 1], bytes[i + 2])
          i += 3
        elsif remaining >= 3
          # SysEx continue
          @transport.send_packet(@cable, CIN_SYSEX_START,
                                 bytes[i], bytes[i + 1], bytes[i + 2])
          i += 3
        else
          break
        end
      end
    end

    private

    # Transport mask constants (must match midi.h's MIDI_TRANSPORT_*).
    # The bit values intentionally coincide with the Transport interface's
    # MIDI_TRANSPORT_ID_USB (1) and MIDI_TRANSPORT_ID_SERIAL (2) so a
    # transport's id doubles as its mask bit on the trigger/batch path.
    TRANSPORT_USB        = 0x01
    TRANSPORT_SAM2695    = 0x02
    TRANSPORT_ALL        = 0x03  # legacy fallback: physical outputs only
    TRANSPORT_USB_DEVICE = 0x04  # ESP32 as USB-MIDI device (Tab5 USB-C)

    # Determine transport mask using the transport's declared id, falling
    # back to the legacy class-name match for transports that haven't yet
    # exposed a transport_id (e.g. user-defined transports).
    def _get_transport_mask
      if @transport.respond_to?(:transport_id)
        @transport.transport_id
      else
        case @transport.class.to_s
        when "USB_MIDI"        then TRANSPORT_USB
        when "SAM2695"         then TRANSPORT_SAM2695
        when "USB_MIDI_DEVICE" then TRANSPORT_USB_DEVICE
        else                        TRANSPORT_ALL
        end
      end
    end

    # Helper to send a 3-byte channel message
    def send_channel_message(msg_type, channel, data1, data2)
      status = msg_type | (channel & 0x0F)
      cin = (msg_type >> 4) & 0x0F
      @transport.send_packet(@cable, cin, status, data1, data2)
    end
  end
end

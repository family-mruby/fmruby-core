# Family mruby OS - MIDI support
#
# Turns MIDI messages into sound on the built-in NES APU. The transport
# implements the five methods MIDI::Device expects (picoruby-midi), so an
# app writes plain MIDI:
#
#   device = FmrbMidi.device(self)     # inside an FmrbApp
#   device.note_on(60, 100)
#   device.note_off(60)
#
# What the APU can do bounds what arrives here: four monophonic voices
# (two pulses, one triangle, one noise), so sixteen MIDI channels and any
# chord have to be reduced. The rules match tool/midi/smf2fmsq.rb, which
# performs the same reduction offline; a tune must sound the same whether it
# was converted to FMSQ beforehand or played live through this transport.
#
# Voices live on the SUB APU instance, because FmrbAudio#note_on does. That
# keeps this independent of FMSQ music playing on MAIN (they are mixed, not
# shared), but it does collide with FMSQ sound effects started with
# play_slot(instance: 1) - this transport assumes it owns SUB.

module FmrbMidi
  # APU channel indices, matching FMRB_APU_CH_* on the audio side.
  CH_PULSE1   = 0
  CH_PULSE2   = 1
  CH_TRIANGLE = 2
  CH_NOISE    = 3

  VOICE_NAMES = {
    pulse1: CH_PULSE1, pulse2: CH_PULSE2,
    triangle: CH_TRIANGLE, noise: CH_NOISE
  }

  # MIDI note -> frequency argument for FmrbAudio#note_on.
  #
  # Not computed from 440 * 2**((n-69)/12.0): the audio side derives the APU
  # timer with integer division, so the rounded ideal frequency lands on a
  # different timer than the offline converter writes - the same tune would
  # play at a slightly different pitch depending on the path. These values
  # are chosen so both paths reach the same timer wherever an integer Hz can.
  # Regenerate with tool/midi/gen_apu_note_table.rb.

  # MIDI note -> Hz for the pulse channels (16x divider).
  PULSE_FREQ = [
       55,    55,    55,    55,    55,    55,    55,    55,    55,    55,    55,    55,  # 0
       55,    55,    55,    55,    55,    55,    55,    55,    55,    55,    55,    55,  # 12
       55,    55,    55,    55,    55,    55,    55,    55,    55,    55,    58,    62,  # 24
       65,    69,    73,    78,    82,    87,    92,    98,   104,   110,   117,   123,  # 36
      131,   139,   147,   156,   165,   174,   185,   196,   207,   220,   233,   247,  # 48
      261,   277,   293,   310,   330,   349,   370,   392,   415,   440,   466,   494,  # 60
      522,   553,   587,   621,   658,   698,   740,   782,   828,   880,   932,   988,  # 72
     1045,  1107,  1175,  1242,  1316,  1397,  1471,  1568,  1662,  1747,  1864,  1962,  # 84
     2103,  2229,  2330,  2485,  2655,  2794,  2943,  3107,  3290,  3495,  3728,  3987,  # 96
     4142,  4466,  4660,  5077,  5319,  5588,  5887,  6214,  6580,  6991,  7457,  7982,  # 108
     8597,  8604,  9321, 10161, 10169, 11178, 12421, 12428  # 120
  ]

  # MIDI note -> Hz for the triangle channel (32x divider).
  TRIANGLE_FREQ = [
       28,    28,    28,    28,    28,    28,    28,    28,    28,    28,    28,    28,  # 0
       28,    28,    28,    28,    28,    28,    28,    28,    28,    28,    29,    31,  # 12
       33,    35,    37,    39,    41,    44,    46,    49,    52,    55,    58,    62,  # 24
       65,    69,    73,    78,    82,    87,    92,    98,   104,   110,   117,   123,  # 36
      131,   138,   147,   155,   165,   175,   185,   196,   208,   220,   233,   247,  # 48
      261,   276,   294,   310,   329,   349,   370,   391,   414,   440,   466,   494,  # 60
      522,   553,   587,   621,   658,   698,   735,   784,   831,   873,   932,   981,  # 72
     1047,  1111,  1165,  1242,  1324,  1397,  1471,  1553,  1645,  1747,  1864,  1990,  # 84
     2071,  2229,  2330,  2534,  2655,  2794,  2943,  3107,  3290,  3495,  3728,  3987,  # 96
     4294,  4302,  4660,  5077,  5084,  5588,  6206,  6214,  6983,  6991,  7982,  7982,  # 108
     7990,  9314,  9321,  9321, 11178, 11178, 11186, 13975  # 120
  ]

  # General MIDI percussion -> [noise period, short mode]. Period 0 is the
  # highest pitch and 15 the lowest rumble. Same table as smf2fmsq.rb, minus
  # the decay column: the offline converter can write a fade because it owns
  # every frame, while here a hit lasts until its note off.
  DRUM_MAP = {
    35 => [13, false], 36 => [13, false],  # bass drum
    37 => [6, true],                       # side stick
    38 => [8, false], 40 => [8, false],    # snare
    39 => [7, false],                      # hand clap
    41 => [11, false], 43 => [10, false],  # floor toms
    45 => [10, false], 47 => [9, false],
    48 => [9, false], 50 => [8, false],    # toms
    42 => [3, true], 44 => [3, true],      # closed / pedal hi-hat
    46 => [2, false],                      # open hi-hat
    49 => [1, false], 52 => [1, false],
    57 => [1, false],                      # cymbals
    51 => [2, false]                       # ride
  }
  DRUM_DEFAULT = [6, false]

  # Control changes this transport acts on. Everything else is accepted and
  # ignored, which is what a MIDI receiver is supposed to do.
  CC_VOLUME        = 7
  CC_DUTY          = 70   # sound controller 1: pulse duty 0-3
  CC_ALL_SOUND_OFF = 120
  CC_ALL_NOTES_OFF = 123

  class << self
    # Transports get registered so the MIDI._trigger / MIDI._send_batch
    # stand-ins (which the C layer would normally provide, and which carry no
    # transport reference) can reach them.
    def register(transport)
      @transports = [] unless @transports
      @transports << transport unless @transports.include?(transport)
      transport
    end

    def unregister(transport)
      @transports.delete(transport) if @transports
    end

    def transports
      @transports = [] unless @transports
      @transports
    end

    # Ready-made device for an app: FmrbMidi.device(self) inside an FmrbApp.
    def device(app)
      ::MIDI::Device.new(ApuTransport.new(app))
    end

    # Run every registered transport's pending note offs.
    def tick
      list = transports
      i = 0
      while i < list.size
        list[i].tick
        i += 1
      end
    end

    # How long an app may sleep without a scheduled note off running late.
    # Pass the delay the app wanted; the earlier of the two comes back:
    #
    #   def on_update
    #     FmrbMidi.tick
    #     FmrbMidi.tick_delay(my_next_step_ms)
    #   end
    #
    # Without this, a note started with device.trigger stays on until the
    # app's next update, so short notes come out as long as the update
    # interval.
    def tick_delay(default_ms)
      delay = default_ms
      list = transports
      i = 0
      while i < list.size
        due = list[i].next_due_in
        delay = due if due && due < delay
        i += 1
      end
      delay < 0 ? 0 : delay
    end
  end

  # The note-off half of MIDI::Device#trigger.
  #
  # picoruby-midi normally has a C timer send the note off; Family mruby has
  # no such timer, so the app pumps instead (FmrbMidi.tick, or any further
  # MIDI message). Both transports need exactly this, so it lives here.
  #
  # A transport that includes it must provide note_on(channel, note,
  # velocity) and note_off(channel, note), and start @pending as an array.
  module NoteScheduler
    # Send a note now and its note off later.
    def trigger(channel, note, velocity, duration_ms)
      note_on(channel, note, velocity)
      @pending << [Machine.board_millis + duration_ms, channel, note]
      0
    end

    # Milliseconds until the next scheduled note off, or nil when nothing is
    # pending. Something already overdue reports 0.
    def next_due_in
      return nil if @pending.nil? || @pending.empty?

      now = Machine.board_millis
      best = nil
      i = 0
      while i < @pending.size
        wait = @pending[i][0] - now
        wait = 0 if wait < 0
        best = wait if best.nil? || wait < best
        i += 1
      end
      best
    end

    # Fire whatever note offs have come due.
    def tick
      return if @pending.nil? || @pending.empty?

      now = Machine.board_millis
      keep = []
      i = 0
      while i < @pending.size
        entry = @pending[i]
        if entry[0] <= now
          note_off(entry[1], entry[2])
        else
          keep << entry
        end
        i += 1
      end
      @pending = keep
    end
  end

  class ApuTransport
    include NoteScheduler

    # MIDI channel (0-15, as MIDI::Device counts them) -> APU voice.
    # Channels 1/2/3/10 in the one-based numbering musicians use.
    DEFAULT_MAP = { 0 => CH_PULSE1, 1 => CH_PULSE2, 2 => CH_TRIANGLE, 9 => CH_NOISE }

    attr_reader :audio

    # @param owner [FmrbApp, FmrbAudio] the app to send audio messages for,
    #   or an FmrbAudio instance to reuse
    def initialize(owner)
      # Tested by class, not with respond_to?(:note_on): an app is very
      # likely to have a note_on of its own (a MIDI app especially), and
      # duck typing would then quietly use the app as the audio object.
      @audio = owner.is_a?(FmrbAudio) ? owner : FmrbAudio.new(owner)

      @map = []
      i = 0
      while i < 16
        @map[i] = FmrbMidi::ApuTransport::DEFAULT_MAP[i]
        i += 1
      end

      # Per voice: the notes currently held down, oldest first, and the one
      # actually sounding.
      @held = [[], [], [], []]
      @current = [nil, nil, nil, nil]

      # Per MIDI channel controller state.
      @cc_volume = []
      @cc_duty = []
      i = 0
      while i < 16
        @cc_volume[i] = 127
        @cc_duty[i] = 2 # 50% square
        i += 1
      end

      @pending = [] # scheduled note offs: [due_ms, channel, note]
      FmrbMidi.register(self)
    end

    # --- Transport interface expected by MIDI::Device --------------------

    def send_packet(_cable, _cin, b1, b2, b3)
      tick # flush note offs that came due since the last message

      status = b1 & 0xF0
      channel = b1 & 0x0F
      case status
      when 0x90
        # Note On with velocity 0 is the usual spelling of Note Off.
        b3 == 0 ? note_off(channel, b2) : note_on(channel, b2, b3)
      when 0x80
        note_off(channel, b2)
      when 0xB0
        control_change(channel, b2, b3)
      end
      # Program change, pitch bend, aftertouch, realtime and SysEx are
      # accepted and dropped: the APU has no patches and no pitch wheel.
      0
    end

    # Send only: there is no MIDI input from the APU.
    def read_available
      ""
    end

    def bytes_available
      0
    end

    def connected?
      true
    end

    def device_info
      { name: "Family mruby APU", voices: 4, direction: :out }
    end

    # MIDI_TRANSPORT_ID_NONE: this is not USB, serial or BLE. Only the
    # trigger path in MIDI::Device looks at it, and the stand-in here
    # ignores masks.
    def transport_id
      0
    end

    # --- Channel assignment ----------------------------------------------

    # Route a MIDI channel to a voice, or to nothing with a nil voice.
    # @param channel [Integer] MIDI channel 0-15
    # @param voice [Symbol, Integer, nil] :pulse1 :pulse2 :triangle :noise
    def map_channel(channel, voice)
      return nil unless channel.is_a?(Integer) && channel >= 0 && channel < 16

      index = voice.is_a?(Symbol) ? FmrbMidi::VOICE_NAMES[voice] : voice
      silence_channel(channel)
      @map[channel] = index
    end

    def voice_for(channel)
      @map[channel & 0x0F]
    end

    # Assign the voices from what a song actually uses, given the usage hash
    # {channel => [note count, pitch sum]} that SmfPlayer#channel_usage
    # returns. Files in the wild rarely use channels 0/1/2/9, so without this
    # a real song plays silently on the default map.
    #
    # Same rule as tool/midi/smf2fmsq.rb --auto: channel 9 is percussion by
    # convention, then the three busiest channels take the melodic voices,
    # highest average pitch first, so the lead lands on a pulse and the bass
    # on the triangle.
    def auto_map(usage)
      i = 0
      while i < 16
        @map[i] = nil
        i += 1
      end
      return @map if usage.nil? || usage.empty?

      @map[9] = FmrbMidi::CH_NOISE if usage[9]

      melodic = []
      usage.each do |channel, stats|
        melodic << [channel, stats[0], stats[1] / stats[0]] unless channel == 9
      end
      # Busiest first, then the top three by average pitch.
      melodic = sort_by_desc(melodic, 1)
      melodic = melodic[0, 3] if melodic.size > 3
      melodic = sort_by_desc(melodic, 2)

      voices = [FmrbMidi::CH_PULSE1, FmrbMidi::CH_PULSE2, FmrbMidi::CH_TRIANGLE]
      i = 0
      while i < melodic.size
        @map[melodic[i][0]] = voices[i]
        i += 1
      end
      @map
    end

    # Scheduled note offs come from NoteScheduler, which every transport
    # includes so MIDI::Device#trigger works whatever the output is.

    # --- Note handling ----------------------------------------------------

    def note_on(channel, note, velocity)
      voice = @map[channel]
      return nil if voice.nil?

      held = @held[voice]
      remove_note(held, note)
      held << [note, velocity, channel]
      sound(voice, note, velocity, channel)
    end

    def note_off(channel, note)
      voice = @map[channel]
      return nil if voice.nil?

      held = @held[voice]
      was_current = @current[voice] == note
      remove_note(held, note)
      return nil unless was_current

      if held.empty?
        silence(voice)
      else
        # Fall back to the newest note still held, the way a monophonic
        # synthesizer does, so overlapping (legato) notes stay connected.
        last = held[held.size - 1]
        sound(voice, last[0], last[1], last[2])
      end
    end

    def control_change(channel, cc, value)
      case cc
      when FmrbMidi::CC_VOLUME
        @cc_volume[channel] = value
      when FmrbMidi::CC_DUTY
        @cc_duty[channel] = (value >> 5) & 0x03
      when FmrbMidi::CC_ALL_SOUND_OFF, FmrbMidi::CC_ALL_NOTES_OFF
        silence_channel(channel)
      end
      0
    end

    # Stop everything this transport is sounding.
    def all_off
      i = 0
      while i < 16
        silence_channel(i)
        i += 1
      end
      @pending = []
      0
    end

    private

    # Insertion sort on one column, descending. Written out because
    # sort_by with a block is heavier than it looks on this VM and the
    # lists here are at most sixteen entries.
    def sort_by_desc(rows, column)
      i = 1
      while i < rows.size
        row = rows[i]
        j = i - 1
        while j >= 0 && rows[j][column] < row[column]
          rows[j + 1] = rows[j]
          j -= 1
        end
        rows[j + 1] = row
        i += 1
      end
      rows
    end

    def remove_note(held, note)
      i = 0
      while i < held.size
        if held[i][0] == note
          held.delete_at(i)
          return true
        end
        i += 1
      end
      false
    end

    def silence_channel(channel)
      voice = @map[channel]
      return if voice.nil?

      @held[voice] = []
      silence(voice)
    end

    def sound(voice, note, velocity, channel)
      @current[voice] = note
      case voice
      when FmrbMidi::CH_TRIANGLE
        # The triangle has no volume control, so velocity is dropped.
        @audio.note_on(voice, FmrbMidi::TRIANGLE_FREQ[note & 0x7F], 0, 0, 0)
      when FmrbMidi::CH_NOISE
        drum = FmrbMidi::DRUM_MAP[note] || FmrbMidi::DRUM_DEFAULT
        # The audio side reads the noise period from the low 4 bits of the
        # frequency argument and the short-mode flag from bit 7.
        arg = (drum[0] & 0x0F) | (drum[1] ? 0x80 : 0x00)
        @audio.note_on(voice, arg, volume_for(channel, velocity), 0, 0)
      else
        @audio.note_on(voice, FmrbMidi::PULSE_FREQ[note & 0x7F],
                       volume_for(channel, velocity), @cc_duty[channel], 0)
      end
      0
    end

    def silence(voice)
      @current[voice] = nil
      @audio.note_off(voice)
      0
    end

    # MIDI velocity (0-127) scaled by channel volume -> APU volume (1-15).
    # Linear, like smf2fmsq.rb: the ratio of the velocity carries over to
    # the amplitude, which keeps accents intact.
    def volume_for(channel, velocity)
      return 0 if velocity <= 0

      scaled = (velocity * @cc_volume[channel]) / 127
      volume = ((scaled * 15) + 63) / 127
      volume = 1 if volume < 1
      volume = 15 if volume > 15
      volume
    end
  end
end

# Stand-ins for the two MIDI::Device methods that would otherwise call into
# picoruby-midi's C scheduler, which Family mruby does not build. They keep
# device.trigger / device.trigger_batch working, with one difference worth
# knowing: the C version sends the note off from a timer, while this one
# needs the app to pump (FmrbMidi.tick, or any further MIDI message).
module MIDI
  class << self
    # The mask is the calling device's transport_id (MIDI::Device works it
    # out from the transport). Only transports of that kind are triggered,
    # so a note meant for the APU does not also go out of the serial port.
    def _trigger(transport_mask, channel, note, velocity, duration_ms)
      list = ::FmrbMidi.transports
      i = 0
      while i < list.size
        transport = list[i]
        i += 1
        next unless transport.transport_id == transport_mask

        transport.trigger(channel, note, velocity, duration_ms)
      end
      0
    end

    def _send_batch(events)
      sent = 0
      i = 0
      while i < events.size
        event = events[i]
        if event[:type] == :note_on
          _trigger(event[:transport], event[:channel], event[:note],
                   event[:velocity] || 100, event[:duration_ms] || 1000)
          sent += 1
        end
        i += 1
      end
      sent
    end
  end
end

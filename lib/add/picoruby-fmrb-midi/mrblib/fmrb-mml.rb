# Family mruby OS - MML player
#
# Plays a tune written as text through a MIDI::Device, so the same string
# drives the built-in APU and an external instrument:
#
#   player = FmrbMidi::MmlPlayer.new(FmrbMidi.device(self))
#   player.load_string("o4 l4 cdefgab>c")
#   player.bpm = 120
#   player.start
#
#   def on_update            # in an FmrbApp
#     player.next_delay(100) # hand over what is due, get the time to sleep
#   end
#
# The MML dialect is whatever MIDI::MML::Sequence parses (imported unchanged
# from Midori; see lib/add/picoruby-midi-mml/FAMILY_MRUBY_PORT.md). What is
# ours is the playing: Midori's player is driven by a clock loop that calls
# it, so a note sounds when that loop happens to run, while this one hands
# timed commands to the C queue and a timer sends them at the microsecond
# the score asks for (doc/midi/report/p7_6.md). Notes land within a
# millisecond of where they belong instead of within an app update.
#
# It is the same shape as SmfPlayer on purpose - load, start, tick, stop,
# tempo_scale - so an app can swap one for the other, and so both get the
# scheduler and the voice grouping from the same code.
#
# Garbage: parsing allocates (Hashes, Strings, an Array per event); playing
# allocates nothing. The parsed events are packed into Integers at load and
# played from there, because a collection during playback is audible
# (doc/midi/report/p6.md).

module FmrbMidi
  class MmlPlayer
    # A quarter note is 24 clocks in MIDI::MML::Sequence, so a whole note is
    # 96. Kept here as well because the conversion to microseconds is this
    # player's job, not the parser's.
    CLOCKS_PER_QUARTER = 24

    # The parser has no tempo command (a "t120" in the string is ignored),
    # so the tempo belongs to the player.
    DEFAULT_BPM = 120

    # What a "voice" line in a tune file may name, as map_channel numbers them.
    VOICE_CHANNELS = {
      "pulse1" => FmrbMidi::CH_PULSE1, "pulse2" => FmrbMidi::CH_PULSE2,
      "triangle" => FmrbMidi::CH_TRIANGLE, "noise" => FmrbMidi::CH_NOISE,
      "0" => FmrbMidi::CH_PULSE1, "1" => FmrbMidi::CH_PULSE2,
      "2" => FmrbMidi::CH_TRIANGLE, "3" => FmrbMidi::CH_NOISE
    }

    # Packed event layout. Everything a note needs fits in one Integer, so a
    # tune costs one Array of Integers and playing it costs nothing.
    #
    #   bits  0-6   note number
    #   bits  7-13  velocity
    #   bits 14-17  MIDI channel
    #   bit  18     1 = note on, 0 = note off
    #   bits 19-    clock position
    #
    # Sorting the packed values sorts by clock first and, within a clock, by
    # note off before note on - which is the order a monophonic voice wants,
    # since the note being released has to let go before the next one takes
    # the voice.
    EV_NOTE_MASK = 0x7F
    EV_VELOCITY_SHIFT = 7
    EV_CHANNEL_SHIFT = 14
    EV_ON_BIT = 1 << 18
    EV_CLOCK_SHIFT = 19

    # Same as SmfPlayer: how far ahead the queue is filled, how much room to
    # leave in it, and how long to wait while the tail plays out.
    LOOKAHEAD_US = 300_000
    QUEUE_MARGIN = 16
    DRAIN_POLL_MS = 20
    STALL_US = 200_000

    attr_reader :device, :bpm, :total_clocks, :error

    def initialize(device)
      @sched = FmrbMidi.scheduler
      @device = device
      @voices = voice_group_of(device)
      @lookahead_us = @voices && @sched ? LOOKAHEAD_US : 0
      @bpm = DEFAULT_BPM
      @loop = false
      reset_state
    end

    # Point the tune at a different output while it plays.
    def device=(new_device)
      drop_queued
      silence
      @device = new_device
      @voices = voice_group_of(new_device)
      @lookahead_us = @voices && @sched ? LOOKAHEAD_US : 0
    end

    # See SmfPlayer#voice_group_of: an output that wants to be told where one
    # musical instant ends, or nil.
    def voice_group_of(device)
      return nil unless device.respond_to?(:transport)

      transport = device.transport
      transport.respond_to?(:flush_voices) ? transport : nil
    end

    # --- loading ---------------------------------------------------------

    # Parse an MML string and make it the tune. Returns true, or false when
    # the string yields nothing playable.
    def load_string(mml, channel: 0, velocity: 100)
      stop
      reset_state
      add_string(mml, channel: channel, velocity: velocity)
    end

    # Load a tune written as a file, so a song can be edited and shipped like
    # any other asset instead of living inside the program that plays it
    # (tools/fm_asset_editor writes these). The format is line based:
    #
    #   # a comment, at the start of a line only ('#' is a sharp in a part)
    #   bpm 120          the tempo, which the MML dialect has no command for
    #   loop on          repeat at the end (default off)
    #   velocity 80      applies to the parts below it (default 100)
    #   voice triangle   which APU voice plays it (pulse1/pulse2/triangle/noise)
    #   duty 1           pulse width 0-3 (12.5, 25, 50, 75 per cent)
    #   volume 100       channel volume 0-127
    #   program 24       instrument for an external MIDI sound source (GM)
    #   o5 l4 cegegegc   a part; each one goes on its own channel, in order
    #
    # The four sound settings say what plays a part, which the dialect has no
    # way to express: they are sent to the device as it is loaded (the voice as
    # a channel mapping, the rest as control and program changes), and a device
    # that has no use for one ignores it. Leaving them out leaves the machine's
    # defaults alone.
    #
    # Returns true, or false with #error saying what was wrong.
    def load_file(path)
      text = nil
      begin
        File.open(path, "r") { |f| text = f.read }
      rescue => e
        stop
        reset_state
        @error = "#{path}: #{e.message}"
        return false
      end
      load_text(text)
    end

    # The body of load_file, split out so a tune held in memory (one already
    # read, or one built by a program) goes through the same rules.
    def load_text(text)
      stop
      reset_state
      velocity = 100
      voice = nil
      duty = nil
      volume = nil
      program = nil
      channel = 0
      text.to_s.split("\n").each do |raw|
        line = raw.strip
        next if line.empty? || line.start_with?("#")

        key, argument = split_setting(line)
        case key
        when "bpm"      then self.bpm = argument.to_i
        when "velocity" then velocity = argument.to_i
        when "loop"     then @loop = %w[on yes true 1].include?(argument)
        when "voice"    then voice = VOICE_CHANNELS[argument]
        when "duty"     then duty = argument.to_i
        when "volume"   then volume = argument.to_i
        when "program"  then program = argument.to_i
        else
          add_string(line, channel: channel, velocity: velocity)
          apply_sound(channel, voice, duty, volume, program)
          channel += 1
        end
      end
      @error = "no parts in the file" if channel == 0
      @loaded
    end

    # Add another part to the tune, on its own channel. Two parts written
    # separately are merged into one sorted list here rather than played by
    # two players: the queue is filled in time order by one producer, and two
    # players filling it independently would interleave out of order.
    def add_string(mml, channel: 0, velocity: 100)
      sequence = ::MIDI::MML::Sequence.new(mml, channel: channel, velocity: velocity)
      pack_events(sequence)
      @total_clocks = sequence.total_length if sequence.total_length > @total_clocks
      @loaded = @events.size > 0
      @error = "no notes in the MML" unless @loaded
      @loaded
    end

    def loaded?
      @loaded
    end

    # How many events the tune holds, for an app that wants to show it.
    # What has actually gone out is sent_count.
    def event_count
      @events.size
    end

    # --- transport controls ----------------------------------------------

    attr_accessor :loop

    def bpm=(value)
      value = 1 if value < 1
      value = 1000 if value > 1000
      # Everything handed over so far keeps the old tempo.
      rebase_clock
      @bpm = value
    end

    def start
      return false unless @loaded

      @index = 0
      @loop_base_clock = 0
      @cur_clock = 0
      @tempo_base_clock = 0
      @tempo_base_us = 0
      @song_base_us = 0
      @sched._reset_stats if @sched
      @playing = true
      @paused = false
      # Anchored again on the first tick, so whatever the app does between
      # asking for the tune and feeding it does not come out of the first
      # notes (doc/midi/report/p7_6.md).
      @clock_pending = true
      @wall_base_us = FmrbMidi.now_us
      true
    end

    def stop
      # Drop the future first, then release what is sounding: the other way
      # round, a queued note on arrives after the note off and hangs.
      drop_queued
      silence
      @playing = false
      @paused = false
      0
    end

    def pause
      return false unless @playing && !@paused

      drop_queued
      silence
      @paused = true
      @pause_us = FmrbMidi.now_us
      true
    end

    def resume
      return false unless @playing && @paused

      @wall_base_us += FmrbMidi.now_us - @pause_us
      @paused = false
      true
    end

    def playing?
      @playing && !@paused
    end

    def finished?
      @loaded && !@playing
    end

    # 1.0 is the tune's own tempo, 2.0 twice as fast. Separate from bpm so an
    # app can offer both "what the piece is" and "how fast to play it".
    def tempo_scale=(value)
      permille = (value * 1000).to_i
      permille = 1 if permille < 1
      permille = 100_000 if permille > 100_000
      rebase_clock
      @scale_permille = permille
    end

    def tempo_scale
      @scale_permille / 1000.0
    end

    # --- the clock -------------------------------------------------------

    attr_reader :stall_count, :underrun_count, :sent_count

    def timing_stats
      n = @sent_count
      return "no events yet" if n.nil? || n == 0

      sched = @sched
      unless sched && @lookahead_us > 0
        return "events=#{n} stalls=#{@stall_count}"
      end

      st = sched._stats
      fired = st[:fired]
      avg = fired > 0 ? st[:late_sum_us] / fired : 0
      "events=#{n} fired=#{fired} avg_late=#{avg}us max_late=#{st[:late_max_us]}us " \
        "under=#{@underrun_count} q=#{sched._depth}/#{st[:depth_max]} " \
        "drop=#{st[:dropped]}/#{st[:send_failed]}"
    end

    # Hand over everything due within the horizon and return the number of
    # milliseconds until this wants calling again (nil when the tune is over).
    def tick
      return nil unless playing?

      now = FmrbMidi.now_us
      if @clock_pending
        @clock_pending = false
        @wall_base_us = now
      end
      wait = run_due(now)
      close_instant
      wait
    end

    # For an app's on_update: the smaller of its own delay and the time to
    # the next event.
    def next_delay(default_ms)
      wait = tick
      return default_ms if wait.nil?

      wait < default_ms ? wait : default_ms
    end

    private

    # Tell the device what should play this channel. The voice is the
    # transport's business (only the APU has voices to hand out); the rest are
    # ordinary MIDI messages, which a transport with no use for them ignores,
    # as a MIDI receiver is supposed to.
    def apply_sound(channel, voice, duty, volume, program)
      return unless @device

      transport = @device.transport
      if voice && transport && transport.respond_to?(:map_channel)
        transport.map_channel(channel, voice)
      end
      # The duty control carries 0-127 and the transport keeps the top two
      # bits, so a duty of 0-3 goes out as 0, 32, 64 or 96.
      @device.control_change(FmrbMidi::CC_DUTY, duty * 32, channel: channel) if duty
      @device.control_change(FmrbMidi::CC_VOLUME, volume, channel: channel) if volume
      @device.program_change(program, channel: channel) if program
    end

    # "bpm 120" -> ["bpm", "120"], and a part line -> its first word, which is
    # no setting and so falls through to being played. Written out rather than
    # with a regexp split to keep it to what picoruby has.
    def split_setting(line)
      space = line.index(" ")
      return [line.downcase, ""] if space.nil?

      [line[0, space].downcase, line[(space + 1)..-1].to_s.strip.downcase]
    end

    def reset_state
      @events = []
      @total_clocks = 0
      @loaded = false
      @playing = false
      @paused = false
      @error = nil
      @index = 0
      @loop_base_clock = 0
      @cur_clock = 0
      @tempo_base_clock = 0
      @tempo_base_us = 0
      @song_base_us = 0
      @wall_base_us = 0
      @pause_us = 0
      @scale_permille = 1000
      @instant_us = nil
      @clock_pending = false
      @sent_count = 0
      @stall_count = 0
      @underrun_count = 0
      @sounding = []
    end

    # Turn the parser's Hashes into packed Integers, once. From here on the
    # tune is Integers and playing it allocates nothing.
    def pack_events(sequence)
      list = sequence.events
      i = 0
      while i < list.size
        event = list[i]
        i += 1
        note = event[:note]
        next if note.nil?

        on = event[:type] == :note_on
        velocity = on ? (event[:velocity] || 100) : 0
        @events << ((event[:clock] << EV_CLOCK_SHIFT) |
                    (on ? EV_ON_BIT : 0) |
                    ((event[:channel] & 0x0F) << EV_CHANNEL_SHIFT) |
                    ((velocity & 0x7F) << EV_VELOCITY_SHIFT) |
                    (note & 0x7F))
      end
      # Clock first, and note off before note on within a clock: that is what
      # the bit layout was chosen for, so a plain numeric sort does it.
      @events.sort!
    end

    # --- musical time ----------------------------------------------------

    # Where a clock falls in the tune, in microseconds.
    #
    # Computed from the clock rather than accumulated per note: a quarter
    # note at 120 BPM is 500000/24 = 20833.3 us, and rounding that at every
    # step would walk the beat off by a third of a millisecond per bar. One
    # multiply before the divide keeps it exact. The tempo base moves only
    # when the tempo changes, so each stretch of one tempo is exact in
    # itself.
    def song_us_for(clock)
      @tempo_base_us +
        (((clock - @tempo_base_clock) * 60_000_000) / (@bpm * CLOCKS_PER_QUARTER))
    end

    def due_us_for(clock)
      wall_us = ((song_us_for(clock) - @song_base_us) * 1000) / @scale_permille
      @wall_base_us + wall_us
    end

    # Freeze the mapping here, so a change of tempo from now on does not move
    # what has already been handed over.
    def rebase_clock
      song_us = song_us_for(@cur_clock)
      wall_us = ((song_us - @song_base_us) * 1000) / @scale_permille
      @wall_base_us += wall_us
      @song_base_us = song_us
      # The tempo itself is about to change under us; hold the tune's time at
      # this clock so the new rate starts from here.
      @tempo_base_us = song_us
      @tempo_base_clock = @cur_clock
    end

    def advance_to(clock)
      @cur_clock = clock
    end

    # --- filling ---------------------------------------------------------

    def run_due(now)
      horizon = now + @lookahead_us
      guard = 0
      while true
        if @index >= @events.size
          if @loop && @total_clocks > 0
            # Round again: the events at the end (the last note off) have
            # been handed over, so the tune can start from the top with its
            # clocks offset by one length.
            @loop_base_clock += @total_clocks
            @index = 0
            next
          end
          return drain_or_finish
        end

        event = @events[@index]
        clock = (event >> EV_CLOCK_SHIFT) + @loop_base_clock
        due = due_us_for(clock)
        if due > horizon
          close_instant
          return ms_until(due - @lookahead_us, now)
        end

        if now - due > STALL_US
          close_instant
          @wall_base_us += (now - due)
          @stall_count += 1
          return 0
        end

        if @sched && @sched._free < QUEUE_MARGIN
          close_instant
          return 1
        end

        if (now - due) > 5_000 && @lookahead_us > 0 && @sched && @sched._depth == 0
          @underrun_count += 1
        end

        advance_to(clock)
        open_instant(due)
        send_event(event)
        @sent_count += 1
        @index += 1
        guard += 1
        if guard >= 256
          close_instant
          return 0
        end
      end
    end

    def drain_or_finish
      close_instant
      if @sched && @sched._depth > 0
        return DRAIN_POLL_MS
      end

      finish
      nil
    end

    def ms_until(target_us, now)
      return 0 if target_us <= now

      ms = (target_us - now) / 1000
      ms < 1 ? 1 : ms
    end

    def open_instant(due_us)
      return 0 if @instant_us == due_us

      close_instant
      @instant_us = due_us
      @voices.defer_voices(@lookahead_us > 0 ? due_us : nil) if @voices
      0
    end

    def close_instant
      return 0 if @instant_us.nil?

      @instant_us = nil
      @voices.flush_voices if @voices
      0
    end

    def drop_queued
      return 0 unless @sched

      close_instant
      @sched._clear
      0
    end

    # --- sending ---------------------------------------------------------

    def send_event(event)
      note = event & EV_NOTE_MASK
      channel = (event >> EV_CHANNEL_SHIFT) & 0x0F
      if (event & EV_ON_BIT) != 0
        @device.send_note_on(channel, note, (event >> EV_VELOCITY_SHIFT) & 0x7F)
        @sounding << ((channel << 7) | note)
      else
        @device.send_note_off(channel, note)
        remove_sounding((channel << 7) | note)
      end
    end

    def remove_sounding(packed)
      i = 0
      while i < @sounding.size
        if @sounding[i] == packed
          @sounding.delete_at(i)
          return
        end
        i += 1
      end
    end

    # Release everything this player started. A tune that loops relies on
    # this too: stopping it mid-round must not leave the round's notes on.
    def silence
      return if @sounding.nil? || @sounding.empty?

      list = @sounding
      @sounding = []
      i = 0
      while i < list.size
        packed = list[i]
        @device.send_note_off(packed >> 7, packed & 0x7F)
        i += 1
      end
    end

    def finish
      silence
      @playing = false
    end
  end
end

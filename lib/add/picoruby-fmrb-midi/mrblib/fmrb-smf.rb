# Family mruby OS - Standard MIDI File player
#
# Plays a .mid straight from the filesystem through a MIDI::Device, so the
# same song can drive the built-in APU today and an external instrument later
# without the player knowing the difference.
#
#   player = FmrbMidi::SmfPlayer.new(FmrbMidi.device(self))
#   player.load("/data/midi/song.mid")
#   player.start
#
#   def on_update          # in an FmrbApp
#     player.tick          # send whatever is due, returns ms to the next event
#   end
#
# Why this exists next to tool/midi/smf2fmsq.rb: converting to FMSQ ahead of
# time plays more accurately and costs almost nothing to run, but the song is
# then fixed. Here the song can be started, stopped, sped up, transposed and
# pointed at a different output while it plays.
#
# Memory: the file stays in one String and events are decoded as they come
# due, so a song costs about its file size (a few KB) instead of one Ruby
# object per event, which would run to hundreds of KB on a busy piece and
# would not fit the app's pool.

module FmrbMidi
  class SmfPlayer
    # Per-track cursor fields.
    T_POS    = 0 # read position in the file
    T_END    = 1 # end of this track chunk
    T_TICK   = 2 # absolute tick of the track's next event
    T_STATUS = 3 # running status byte
    T_DONE   = 4

    DEFAULT_TEMPO_US = 500_000 # 120 BPM until the file says otherwise

    # A tick that arrives more than this late means the app was busy
    # elsewhere. Rather than firing the backlog in one burst, the schedule is
    # shifted so the song simply resumes from where it was.
    STALL_MS = 200

    attr_reader :device, :path, :division, :format, :track_count

    def initialize(device)
      @device = device
      reset_state
    end

    # --- loading ---------------------------------------------------------

    # Read a Standard MIDI File. Returns true, or false when the file cannot
    # be used (missing, not an SMF, or format 2).
    def load(path)
      stop
      # picoruby has no File.binread.
      data = nil
      begin
        File.open(path, "r") { |f| data = f.read }
      rescue => e
        @error = "cannot read #{path}: #{e.message}"
        return false
      end
      load_string(data, path)
    end

    # Same as load, for bytes already in memory.
    def load_string(data, path = nil)
      reset_state
      @data = data
      @path = path
      return false unless parse_header

      collect_tracks
      @loaded = @tracks.size > 0
      @error = "no track chunks" unless @loaded
      @loaded
    end

    attr_reader :error

    def loaded?
      @loaded
    end

    # Which channels the file actually uses: {channel => [note count, pitch
    # sum]}. Real files rarely use channels 1/2/3/10, so this is what makes
    # an arbitrary song playable without the user editing the map by hand.
    # Scans the file without building any per-event objects.
    def channel_usage
      usage = {}
      return usage unless @loaded

      i = 0
      while i < @tracks.size
        scan_track_usage(@tracks[i][T_POS], @tracks[i][T_END], usage)
        i += 1
      end
      usage
    end

    # --- transport controls ----------------------------------------------

    def start
      return false unless @loaded

      rewind
      @playing = true
      @paused = false
      @wall_base_ms = Machine.board_millis
      @song_base_us = 0
      true
    end

    def stop
      silence
      @playing = false
      @paused = false
      0
    end

    def pause
      return false unless @playing && !@paused

      silence
      @paused = true
      @pause_ms = Machine.board_millis
      true
    end

    def resume
      return false unless @playing && @paused

      # Shift the schedule by however long the pause lasted so the song
      # carries on from the same place.
      @wall_base_ms += Machine.board_millis - @pause_ms
      @paused = false
      true
    end

    def playing?
      @playing && !@paused
    end

    def finished?
      @loaded && !@playing
    end

    # Song position in milliseconds (musical time, not wall time).
    def position_ms
      @song_us / 1000
    end

    # --- real-time controls ----------------------------------------------

    # 1.0 is the file's own tempo, 2.0 twice as fast. Takes effect at once,
    # without disturbing the part of the song already played.
    def tempo_scale=(value)
      permille = (value * 1000).to_i
      permille = 1 if permille < 1
      permille = 100_000 if permille > 100_000
      # Re-anchor first: everything up to now keeps the old rate.
      rebase_clock
      @scale_permille = permille
    end

    def tempo_scale
      @scale_permille / 1000.0
    end

    # Semitones added to every melodic note. Percussion (channel 9) is left
    # alone, since its note numbers are instruments and not pitches.
    attr_accessor :transpose

    # Which MIDI channels to play. nil plays everything.
    attr_accessor :channel_mask

    # --- the clock -------------------------------------------------------

    # Send everything that is due and return the number of milliseconds until
    # the next event (0 when something is already due, nil when not playing).
    def tick
      return nil unless playing?

      now = Machine.board_millis
      guard = 0
      while true
        index = next_track
        if index.nil?
          finish
          return nil
        end

        due = due_ms_for(@tracks[index][T_TICK])
        if due > now
          return due - now
        end

        # Falling far behind (the app was busy) shifts the whole schedule
        # instead of firing a burst of notes that should already be over.
        if now - due > STALL_MS
          @wall_base_ms += (now - due)
          return 0
        end

        step(index)
        guard += 1
        # Very dense passages can have hundreds of events on one tick; the
        # guard keeps a pathological file from holding the app task forever.
        return 0 if guard >= 256
      end
    end

    # For an app's on_update: the smaller of its own delay and the time to
    # the next event.
    def next_delay(default_ms)
      wait = tick
      return default_ms if wait.nil?
      wait < default_ms ? wait : default_ms
    end

    private

    def reset_state
      @data = nil
      @path = nil
      @tracks = []
      @loaded = false
      @playing = false
      @paused = false
      @error = nil
      @division = 0
      @format = 0
      @track_count = 0
      @us_per_quarter = DEFAULT_TEMPO_US
      @scale_permille = 1000
      @transpose = 0
      @channel_mask = nil
      @song_us = 0
      @song_base_us = 0
      @wall_base_ms = 0
      @cur_tick = 0
      @pause_ms = 0
      @sounding = []
    end

    # --- file structure --------------------------------------------------

    def parse_header
      if @data.nil? || @data.length < 14
        @error = "file is too short to be an SMF"
        return false
      end
      unless byte(0) == 0x4D && byte(1) == 0x54 && byte(2) == 0x68 && byte(3) == 0x64
        @error = "not a Standard MIDI File (no MThd)"
        return false
      end

      @format = u16(8)
      @track_count = u16(10)
      @division = u16(12)
      if @format == 2
        @error = "SMF format 2 is not supported"
        return false
      end
      if @division == 0
        @error = "division is zero"
        return false
      end
      true
    end

    # Walk the chunk list once and remember where each track starts.
    def collect_tracks
      @tracks = []
      pos = 8 + u32(4)
      size = @data.length
      while pos + 8 <= size
        len = u32(pos + 4)
        if byte(pos) == 0x4D && byte(pos + 1) == 0x54 &&
           byte(pos + 2) == 0x72 && byte(pos + 3) == 0x6B
          @tracks << [pos + 8, pos + 8 + len, 0, 0, false]
        end
        pos += 8 + len
      end
    end

    # One pass over a track chunk, counting note ons per channel. Deltas and
    # payloads are skipped without decoding them into anything.
    def scan_track_usage(pos, finish_pos, usage)
      status = 0
      while pos < finish_pos
        _delta, pos = varlen(pos)
        b = byte(pos)
        if b >= 0x80
          status = b if b < 0xF0
          meta = b
          pos += 1
        else
          meta = status
        end

        case meta & 0xF0
        when 0x90
          note = byte(pos)
          velocity = byte(pos + 1)
          pos += 2
          if velocity > 0
            channel = meta & 0x0F
            entry = usage[channel]
            if entry
              entry[0] += 1
              entry[1] += note
            else
              usage[channel] = [1, note]
            end
          end
        when 0x80, 0xA0, 0xB0, 0xE0
          pos += 2
        when 0xC0, 0xD0
          pos += 1
        else
          case meta
          when 0xFF
            pos += 1 # meta type
            len, pos = varlen(pos)
            pos += len
          when 0xF0, 0xF7
            len, pos = varlen(pos)
            pos += len
          else
            return # cannot make sense of this track
          end
        end
      end
    end

    def rewind
      # Rebuilding the cursors from the chunk list is the cheapest way to get
      # back to the beginning, and it makes start callable more than once.
      collect_tracks
      @us_per_quarter = DEFAULT_TEMPO_US
      @song_us = 0
      @song_base_us = 0
      @cur_tick = 0
      @sounding = []
      i = 0
      while i < @tracks.size
        read_next_delta(@tracks[i])
        i += 1
      end
    end

    def byte(i)
      @data.getbyte(i) || 0
    end

    def u16(i)
      (byte(i) << 8) | byte(i + 1)
    end

    def u32(i)
      (byte(i) << 24) | (byte(i + 1) << 16) | (byte(i + 2) << 8) | byte(i + 3)
    end

    # Variable length quantity; returns [value, next position].
    def varlen(pos)
      value = 0
      while true
        b = byte(pos)
        pos += 1
        value = (value << 7) | (b & 0x7F)
        break if (b & 0x80) == 0
        # Malformed files must not spin forever.
        break if pos > @data.length
      end
      [value, pos]
    end

    # Read the delta time in front of a track's next event and turn it into
    # an absolute tick.
    def read_next_delta(track)
      if track[T_POS] >= track[T_END]
        track[T_DONE] = true
        return
      end

      delta, pos = varlen(track[T_POS])
      track[T_POS] = pos
      track[T_TICK] = track[T_TICK] + delta
    end

    # Track holding the earliest event; ties go to the lower track number so
    # playback is reproducible.
    def next_track
      best = nil
      i = 0
      while i < @tracks.size
        t = @tracks[i]
        unless t[T_DONE]
          best = i if best.nil? || t[T_TICK] < @tracks[best][T_TICK]
        end
        i += 1
      end
      best
    end

    # --- musical time ----------------------------------------------------

    # Microseconds for a span of ticks at the current tempo, in a way that
    # keeps every intermediate value small enough for a 32-bit integer.
    def ticks_to_us(ticks)
      whole = @us_per_quarter / @division
      rest = @us_per_quarter % @division
      (ticks * whole) + ((ticks * rest) / @division)
    end

    # Wall clock time at which a tick should sound.
    def due_ms_for(tick)
      song_us = @song_us + ticks_to_us(tick - @cur_tick)
      wall_us = ((song_us - @song_base_us) * 1000) / @scale_permille
      @wall_base_ms + (wall_us / 1000)
    end

    # Freeze the mapping at the current position, so a tempo change from here
    # on does not move notes that already played.
    def rebase_clock
      now_song_us = @song_us
      wall_us = ((now_song_us - @song_base_us) * 1000) / @scale_permille
      @wall_base_ms += wall_us / 1000
      @song_base_us = now_song_us
    end

    # --- event dispatch --------------------------------------------------

    def step(index)
      track = @tracks[index]
      # Advance musical time to this event.
      @song_us += ticks_to_us(track[T_TICK] - @cur_tick)
      @cur_tick = track[T_TICK]

      pos = track[T_POS]
      b = byte(pos)
      if b >= 0x80
        status = b
        pos += 1
        track[T_STATUS] = status if status < 0xF0
      else
        status = track[T_STATUS]
        if status == 0
          # Nothing sensible to do with a data byte and no status; drop the
          # rest of this track rather than desynchronize.
          track[T_DONE] = true
          return
        end
      end

      case status & 0xF0
      when 0x80, 0x90, 0xA0, 0xB0, 0xE0
        d1 = byte(pos)
        d2 = byte(pos + 1)
        pos += 2
        handle_channel_event(status, d1, d2)
      when 0xC0, 0xD0
        d1 = byte(pos)
        pos += 1
        handle_channel_event(status, d1, 0)
      else
        case status
        when 0xFF
          type = byte(pos)
          pos += 1
          len, pos = varlen(pos)
          handle_meta(type, pos, len)
          if type == 0x2F
            track[T_DONE] = true
            track[T_POS] = track[T_END]
            return
          end
          pos += len
        when 0xF0, 0xF7
          len, pos = varlen(pos)
          pos += len
        else
          # Unknown status byte: stop this track instead of guessing.
          track[T_DONE] = true
          return
        end
      end

      track[T_POS] = pos
      read_next_delta(track)
    end

    def handle_channel_event(status, d1, d2)
      channel = status & 0x0F
      return if @channel_mask && ((@channel_mask >> channel) & 1) == 0

      case status & 0xF0
      when 0x90
        if d2 == 0
          send_note_off(channel, d1)
        else
          send_note_on(channel, d1, d2)
        end
      when 0x80
        send_note_off(channel, d1)
      when 0xB0
        @device.control_change(d1, d2, channel: channel)
      when 0xC0
        @device.program_change(d1, channel: channel)
      end
      # Aftertouch and pitch bend are dropped: the APU has neither, and
      # MIDI::Device#pitch_bend needs Integer#clamp, which this build of
      # mruby does not provide (see doc/midi/report/p2.md).
    end

    def handle_meta(type, pos, len)
      return unless type == 0x51 && len == 3

      value = (byte(pos) << 16) | (byte(pos + 1) << 8) | byte(pos + 2)
      return if value == 0

      # A tempo change starts a new mapping from this point on.
      rebase_clock
      @us_per_quarter = value
    end

    def shift(channel, note)
      return note if channel == 9 || @transpose == 0

      n = note + @transpose
      return note if n < 0 || n > 127

      n
    end

    def send_note_on(channel, note, velocity)
      played = shift(channel, note)
      @device.note_on(played, velocity, channel: channel)
      @sounding << [channel, played]
    end

    def send_note_off(channel, note)
      played = shift(channel, note)
      @device.note_off(played, 0, channel: channel)
      i = 0
      while i < @sounding.size
        entry = @sounding[i]
        if entry[0] == channel && entry[1] == played
          @sounding.delete_at(i)
          return
        end
        i += 1
      end
    end

    # Release everything this player started. Called on stop, pause and at
    # the end of the song, so a stuck note can only outlive the player if the
    # app dies outright.
    def silence
      return if @sounding.nil? || @sounding.empty?

      list = @sounding
      @sounding = []
      i = 0
      while i < list.size
        @device.note_off(list[i][1], 0, channel: list[i][0])
        i += 1
      end
    end

    def finish
      silence
      @playing = false
    end
  end
end

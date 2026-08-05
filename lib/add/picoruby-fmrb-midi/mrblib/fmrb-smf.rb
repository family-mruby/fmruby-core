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
#
# Garbage: playing a song allocates nothing at all, and neither does
# channel_usage. That is not tidiness, it is the difference between a song
# that plays and one that stalls: the collector is the only thing that pauses
# an app task for long enough to hear (doc/midi/report/p6.md). Anything added
# here that allocates per event undoes it.
#
# The output is a MIDI::Device, which here means the one in picoruby-midi
# plus the channel-argument senders that picoruby-fmrb-midi adds to it
# (send_note_on and friends); the keyword-argument spellings allocate.

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
    STALL_US = STALL_MS * 1000

    # Free slots kept in hand before decoding one more instant, so a chord
    # never half-fits. Four is one write per APU voice; the rest is headroom
    # for an output that sends a message per note.
    QUEUE_MARGIN = 16

    # How often to look again while the tail of a song plays out of the
    # queue. Nothing is decoded in that state, so this is only a heartbeat.
    DRAIN_POLL_MS = 20

    # How late a command has to be, with nothing left in the queue, before it
    # counts as the music having gapped. The first instant of a song is due
    # the moment it starts, so a threshold keeps that from reading as a
    # failure.
    UNDERRUN_US = 5_000

    attr_reader :device, :path, :division, :format, :track_count

    # Point the song at a different output while it plays. Whatever is
    # sounding on the old device is released first, otherwise those notes
    # would never see their note off.
    def device=(new_device)
      # Whatever the old output had queued is for an output we are leaving.
      drop_queued
      silence
      @device = new_device
      @voices = voice_group_of(new_device)
      @lookahead_us = @voices && @sched ? LOOKAHEAD_US : 0
    end

    def initialize(device)
      @sched = FmrbMidi.scheduler
      @device = device
      @voices = voice_group_of(device)
      @lookahead_us = @voices && @sched ? LOOKAHEAD_US : 0
      reset_state
    end

    # How far ahead this player fills the queue, in microseconds. Zero puts
    # the sending back on the app's own task, which is what the player did
    # before P7.6 and how the before/after in the report was measured; any
    # other value is capped by what the output can actually take (an output
    # with no queue behind it always reads back 0).
    attr_reader :lookahead_us

    def lookahead_us=(us)
      drop_queued
      @lookahead_us = (us > 0 && @voices && @sched) ? us : 0
    end

    # Drop what is queued for this player and stop anything it left
    # sounding. Used wherever the future stops being the future: stopping,
    # pausing, changing songs or outputs.
    def drop_queued
      return 0 unless @sched

      close_instant
      @sched._clear
      0
    end

    # An output that wants to be told where one musical instant ends, or nil.
    # Only the APU transport does: it has four monophonic voices, so several
    # notes of one chord land on the same voice and it must not play them in
    # turn (see ApuTransport#defer_voices). An external instrument has a
    # voice per note and needs none of this.
    def voice_group_of(device)
      return nil unless device.respond_to?(:transport)

      transport = device.transport
      transport.respond_to?(:flush_voices) ? transport : nil
    end

    # --- loading ---------------------------------------------------------

    # Read a Standard MIDI File. Returns true, or false when the file cannot
    # be used (missing, not an SMF, or format 2).
    def load(path)
      stop
      # Let go of the song already loaded before reading the next one.
      # File#read allocates a buffer of the file's size and then copies it
      # into a String, so loading over the top of a held song needs three
      # copies at once; on a 500 KB app pool that is what makes browsing a
      # directory of songs fail with NoMemoryError (doc/midi/report/p6.md).
      @data = nil
      @tracks = []
      @loaded = false
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
    #
    # This walks every event in the file, so it is written to allocate
    # nothing at all: the totals go into two flat arrays and the Hash is
    # built once at the end. The version that returned [value, position] from
    # a varlen helper and accumulated into a Hash of Arrays took 7.9 seconds
    # on the device for a 22 KB song, five sixths of it inside the collector
    # (doc/midi/report/p6.md).
    def channel_usage
      usage = {}
      return usage unless @loaded

      counts = []
      pitches = []
      i = 0
      while i < 16
        counts[i] = 0
        pitches[i] = 0
        i += 1
      end

      i = 0
      while i < @tracks.size
        scan_track_usage(@tracks[i][T_POS], @tracks[i][T_END], counts, pitches)
        i += 1
      end

      i = 0
      while i < 16
        usage[i] = [counts[i], pitches[i]] if counts[i] > 0
        i += 1
      end
      usage
    end

    # --- transport controls ----------------------------------------------

    def start
      return false unless @loaded

      rewind
      # The scheduler's figures are per song: what matters is how well this
      # one is being played, not the average since boot.
      @sched._reset_stats if @sched
      @playing = true
      @paused = false
      # Anchored again on the first tick: whatever the app does between
      # asking for the song and getting round to feeding it - a redraw is
      # 90 ms in the simulation - would otherwise come out of the first
      # notes, which would then be late before the music had started
      # (doc/midi/report/p7_6.md).
      @clock_pending = true
      @wall_base_us = FmrbMidi.now_us
      @song_base_us = 0
      true
    end

    def stop
      # Order matters: drop the future first, then release what is sounding.
      # The other way round, a queued note on would arrive after the note off
      # and hang (doc/midi/report/p7_6.md).
      drop_queued
      silence
      # Nothing is holding a group open in the normal course of things, but
      # an app that stops the song from inside its own callback could be, and
      # a group left open would swallow every later note.
      @voices.flush_voices if @voices
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

      # Shift the schedule by however long the pause lasted so the song
      # carries on from the same place. The cursor is up to one lookahead
      # past what was actually heard, because pausing dropped what was
      # queued; the song resumes a fraction of a second further on.
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
    # Diagnostics: how late events actually went out, and how often the
    # schedule had to be shifted. With a scheduler in the picture the numbers
    # that matter come from it (how late the sends actually were), so
    # timing_stats reports those and keeps the player's own counters for what
    # only the player can see: how often it fell behind its own decoding.
    attr_reader :late_max_ms, :late_sum_ms, :late_count, :stall_count,
                :underrun_count

    def timing_stats
      n = @late_count
      return "no events yet" if n.nil? || n == 0

      sched = @sched
      # With no queue in front of it - no scheduler, or a lookahead of zero -
      # the player is doing the sending itself, and its own counters are the
      # ones that mean anything.
      unless sched && @lookahead_us > 0
        return "events=#{n} avg_late=#{@late_sum_ms / n}ms " \
               "max_late=#{@late_max_ms}ms stalls=#{@stall_count}"
      end

      st = sched._stats
      fired = st[:fired]
      avg = fired > 0 ? st[:late_sum_us] / fired : 0
      "events=#{n} fired=#{fired} avg_late=#{avg}us max_late=#{st[:late_max_us]}us " \
        "stalls=#{@stall_count} under=#{@underrun_count} " \
        "q=#{sched._depth}/#{st[:depth_max]} drop=#{st[:dropped]}/#{st[:send_failed]}"
    end

    # How far ahead of the music the queue is kept filled.
    #
    # Deep enough that the app can be late by an ordinary amount - a redraw,
    # another task, a collection elsewhere in the system - without the music
    # hearing it; shallow enough that stopping or changing songs is not a
    # noticeable wait, since both are done by dropping what is queued. The
    # device measured app-side lateness in the tens of milliseconds
    # (doc/midi/report/p7.md 9), so 300 ms is roughly ten times the worst
    # case seen.
    LOOKAHEAD_US = 300_000

    # Send whatever is due, or rather: hand the next few hundred milliseconds
    # of music to whatever owns the clock.
    #
    # With a scheduler this decodes ahead and returns when the queue reaches
    # the horizon; the notes go out later, from a timer. Without one (the
    # host tests, and any output that cannot take a time) the horizon is zero
    # and this behaves exactly as it did before: dispatch what is due now.
    #
    # Either way the events of one musical instant are bracketed, so an
    # output with fewer voices than the score can resolve a chord once
    # instead of playing its inner notes in turn (P7.5).
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

    def run_due(now)
      horizon = now + @lookahead_us
      guard = 0
      while true
        index = next_track
        if index.nil?
          return drain_or_finish
        end

        due = due_us_for(@tracks[index][T_TICK])
        if due > horizon
          close_instant
          # Wake when the next event comes into range, not when it sounds.
          return ms_until(due - @lookahead_us, now)
        end

        # Falling far behind (the app was busy) shifts the whole schedule
        # instead of firing a burst of notes that should already be over.
        if now - due > STALL_US
          close_instant
          @wall_base_us += (now - due)
          @stall_count += 1
          return 0
        end

        # Leave room for what this instant will resolve to: up to one write
        # per voice, or one per message on an output that has no voices.
        if @sched && @sched._free < QUEUE_MARGIN
          close_instant
          return 1
        end

        late = now - due
        if late > UNDERRUN_US && @lookahead_us > 0 && @sched && @sched._depth == 0
          # Nothing was left queued and this is already audibly overdue: the
          # music gapped because the decoding did not keep up.
          @underrun_count += 1
        end
        # Only lateness counts: with a lookahead these are normally decoded
        # early, and a negative "lateness" would make the average meaningless.
        late_ms = late > 0 ? late / 1000 : 0
        @late_count += 1
        @late_sum_ms += late_ms
        @late_max_ms = late_ms if late_ms > @late_max_ms

        open_instant(due)
        step(index)
        guard += 1
        # Very dense passages can have hundreds of events on one tick; the
        # guard keeps a pathological file from holding the app task forever.
        if guard >= 256
          close_instant
          return 0
        end
      end
    end

    # The song is decoded to the end; it is over once the queue has played
    # out what is left, and not a moment before.
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

    # Instants are bracketed per musical time, not per call: one tick can
    # hand over several instants when the queue is being filled ahead.
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

    # For an app's on_update: the smaller of its own delay and the time to
    # the next event.
    def next_delay(default_ms)
      wait = tick
      return default_ms if wait.nil?
      wait < default_ms ? wait : default_ms
    end

    private

    def reset_state
      @late_max_ms = 0
      @late_sum_ms = 0
      @late_count = 0
      @stall_count = 0
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
      @wall_base_us = 0
      @cur_tick = 0
      @pause_us = 0
      @vl_pos = 0
      @instant_us = nil
      @clock_pending = false
      @underrun_count = 0
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

    # One pass over a track chunk, adding note ons to counts[channel] and
    # their note numbers to pitches[channel].
    #
    # The variable-length decoding is written out here instead of calling
    # varlen, and the delta time is walked over rather than decoded, because
    # this loop runs once per event in the whole file: on the device a method
    # call per event is the difference between a fifth of a second and
    # several seconds. Everything it touches is an Integer, so a whole song
    # can be scanned without allocating one object.
    def scan_track_usage(pos, finish_pos, counts, pitches)
      data = @data
      status = 0
      while pos < finish_pos
        # Delta time: only its length matters here, so walk off the end of it.
        b = data.getbyte(pos) || 0
        pos += 1
        while b >= 0x80
          b = data.getbyte(pos) || 0
          pos += 1
        end

        b = data.getbyte(pos) || 0
        if b >= 0x80
          status = b if b < 0xF0
          meta = b
          pos += 1
        else
          meta = status
        end

        kind = meta & 0xF0
        if kind == 0x90
          if (data.getbyte(pos + 1) || 0) > 0
            channel = meta & 0x0F
            counts[channel] += 1
            pitches[channel] += (data.getbyte(pos) || 0)
          end
          pos += 2
        elsif kind == 0x80 || kind == 0xA0 || kind == 0xB0 || kind == 0xE0
          pos += 2
        elsif kind == 0xC0 || kind == 0xD0
          pos += 1
        elsif meta == 0xFF
          pos += 1 # meta type
          len = 0
          while true
            b = data.getbyte(pos) || 0
            pos += 1
            len = (len << 7) | (b & 0x7F)
            break if b < 0x80
          end
          pos += len
        elsif meta == 0xF0 || meta == 0xF7
          len = 0
          while true
            b = data.getbyte(pos) || 0
            pos += 1
            len = (len << 7) | (b & 0x7F)
            break if b < 0x80
          end
          pos += len
        else
          return # cannot make sense of this track
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

    # Variable length quantity. Returns the value and leaves the position
    # after it in @vl_pos.
    #
    # Returning [value, position] would read better, but that Array is one
    # object per event, and every event in a song passes through here: it is
    # the single largest source of garbage in playback, and the collector is
    # what makes a song stall on the device (doc/midi/report/p6.md).
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
      @vl_pos = pos
      value
    end

    # Read the delta time in front of a track's next event and turn it into
    # an absolute tick.
    def read_next_delta(track)
      if track[T_POS] >= track[T_END]
        track[T_DONE] = true
        return
      end

      delta = varlen(track[T_POS])
      track[T_POS] = @vl_pos
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
    def due_us_for(tick)
      song_us = @song_us + ticks_to_us(tick - @cur_tick)
      wall_us = ((song_us - @song_base_us) * 1000) / @scale_permille
      @wall_base_us + wall_us
    end

    # Freeze the mapping at the current position, so a tempo change from here
    # on does not move notes that already played.
    def rebase_clock
      now_song_us = @song_us
      wall_us = ((now_song_us - @song_base_us) * 1000) / @scale_permille
      @wall_base_us += wall_us
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
          len = varlen(pos)
          pos = @vl_pos
          handle_meta(type, pos, len)
          if type == 0x2F
            track[T_DONE] = true
            track[T_POS] = track[T_END]
            return
          end
          pos += len
        when 0xF0, 0xF7
          len = varlen(pos)
          pos = @vl_pos + len
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
        @device.send_control_change(channel, d1, d2)
      when 0xC0
        @device.send_program_change(channel, d1)
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

    # What is sounding is kept as packed Integers, (channel << 7) | note,
    # rather than [channel, note] pairs: an Array per note is an object per
    # note, and playback has to leave the collector alone (see varlen).
    def send_note_on(channel, note, velocity)
      played = shift(channel, note)
      @device.send_note_on(channel, played, velocity)
      @sounding << ((channel << 7) | played)
    end

    def send_note_off(channel, note)
      played = shift(channel, note)
      @device.send_note_off(channel, played)
      packed = (channel << 7) | played
      i = 0
      while i < @sounding.size
        if @sounding[i] == packed
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

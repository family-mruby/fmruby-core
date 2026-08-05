# MIDI player benchmark (P6)
#
# The device exposed two costs that the simulation hid: loading a song took
# 7.9 s (all of it in SmfPlayer#channel_usage) and playback stalled now and
# then. Both are cheap here, so guessing from the simulation is what went
# wrong the first time. This app measures them instead:
#
#   scan     the channel scan, as it is in the gem and as a candidate
#   play     the whole song through a counting transport, at 100x
#   alloc    how many objects one dispatched event leaves behind
#
# Every phase reports the GC counters around it, so "is this GC?" is answered
# with numbers. The pause histograms (prof_sync_*) need a measurement build:
#
#   rake clean_all && FMRB_GC_PROFILE=1 rake build:linux
#
# Run it with the debugger client rather than from the launcher:
#
#   python3 tool/debug/fmrb_dbg_client.py localhost:5555 spawn \
#       path=/app/debug/midi_bench.app.rb
#
# Results go to the log (docker compose logs fmruby-core).

class MidiBenchApp < FmrbApp
  SONG = "/usr/share/sounds/midi/joplin_entertainer.mid"

  # Cost ratio between this simulation and the S3, measured on this very
  # scan before P6 touched it: 7900 ms on the device
  # (doc/midi/report/p5s.md 7.2) against 24 ms here, in the demo app's own
  # pool. It is not a clean CPU ratio -- five sixths of both figures was the
  # collector, and the device keeps its heap in PSRAM -- so treat a number
  # projected with it as an upper bound.
  DEVICE_SIM_FACTOR = 330

  # Counts what the player sends without doing anything about it, so the
  # playback figures are the player's own cost and not the APU path's.
  class CountingTransport
    include FmrbMidi::NoteScheduler

    attr_reader :events

    def initialize
      @events = 0
      @pending = []
    end

    def send_packet(_cable, _cin, _b1, _b2, _b3)
      @events += 1
      0
    end

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
      { name: "bench sink", voices: 0, direction: :out }
    end

    def transport_id
      0
    end

    def note_on(_channel, _note, _velocity)
      @events += 1
      0
    end

    def note_off(_channel, _note)
      @events += 1
      0
    end

    def all_off
      0
    end
  end

  def on_create
    @lines = []
    Log.info("midi_bench: on_create")
    begin
      run_all
    rescue Exception => e
      Log.error("midi_bench: #{e.class}: #{e.message}")
    end
    draw_screen
  end

  def on_update
    return 500 if @gc_phase.nil?

    burn_memory
    @gc_updates += 1
    if @gc_updates >= GC_PHASE_UPDATES
      report_gc_phase
      if @gc_phase + 1 < GC_PHASE_MODES.size
        start_gc_phase(@gc_phase + 1)
      else
        self.idle_gc = false
        @gc_phase = nil
        draw_screen
      end
    end
    GC_PHASE_SLEEP
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :key_down

    if (ev[:character] || 0) == 32
      @lines = []
      run_all
      draw_screen
    end
  end

  private

  def say(text)
    Log.info("midi_bench: #{text}")
    @lines << text
    @lines.shift while @lines.size > 14
  end

  def run_all
    unless File.exist?(SONG)
      say("missing #{SONG}")
      return
    end

    # How much of the pool this app's own code took before it allocated
    # anything: compiling a file this size leaves little room in the 500 KB
    # pool, which is why this one asks for the large pool (see the .toml).
    FmrbApp.ps.each do |e|
      say("pool #{e[:name]} #{e[:mem_used]}/#{e[:mem_total]}") if e[:name] == "MIDI Bench"
    end

    data = nil
    t0 = Machine.uptime_us
    File.open(SONG, "r") { |f| data = f.read }
    say("read #{data.length} B in #{(Machine.uptime_us - t0) / 1000}ms")

    gc_report("idle")
    bench_scan(data)
    bench_play
    bench_alloc
    bench_mml
    start_gc_phase(0)
  end

  # --- MML playback ------------------------------------------------------
  #
  # The MML player parses into packed Integers at load and plays from those,
  # so supplying events must cost nothing per event, the same as the .mid
  # player (doc/midi/report/p7_7.md). Measured the same way: with the
  # collector off, the rise in the live count is exactly what was allocated.
  MML_TUNE = "o4 l8 [cdefgab>c<]4"

  def bench_mml
    player = FmrbMidi::MmlPlayer.new(MIDI::Device.new(CountingTransport.new))
    unless player.load_string(MML_TUNE)
      say("mml: #{player.error}")
      return
    end

    say("mml: #{player.event_count} events parsed")
    player.tempo_scale = 100.0
    player.start
    GC.start
    GC.disable
    live0 = GC.stat[:live] || 0
    events0 = player.sent_count
    t0 = Machine.uptime_us
    while player.playing?
      break if player.tick.nil?
      break if Machine.uptime_us - t0 > 5_000_000
    end
    live1 = GC.stat[:live] || 0
    n = player.sent_count - events0
    GC.enable
    player.stop
    GC.start
    say("mml: #{(live1 - live0) * 10 / (n > 0 ? n : 1)} objects/10 events " \
        "(#{live1 - live0} over #{n})")
  end

  # --- idle-time GC (FmrbApp#idle_gc) ------------------------------------
  #
  # The MIDI path allocates nothing per event now, so it cannot show what
  # idle_gc does; this phase supplies the garbage instead. The same amount
  # per update and the same slack left in each _spin, run once with the
  # collector on the allocation path and once on the idle time. What to read
  # is the two pause counters: work should move from pause to step, and the
  # longest single pause should fall.
  #
  # This is also the shape of an app that cannot get its allocation down -
  # a game building objects every frame - which is what idle_gc is for.
  # [label, idle_gc, GC.step_limit]. The first row is the collector on the
  # allocation path (what every app did before P7); the rest move it to the
  # idle time and vary how much work one step may do. 0 means mruby's own
  # 2000. FmrbApp#idle_gc= picks IDLE_GC_STEP_LIMIT by itself; these rows
  # override it so the choice can be seen rather than assumed.
  # nil in the third column leaves whatever FmrbApp#idle_gc= chose, so the
  # shipped default gets measured and not just the overrides.
  GC_PHASE_MODES = [
    [:allocation_path, false, 0],
    [:idle_default, true, nil],
    [:idle_step_2000, true, 0],
    [:idle_step_512, true, 512],
    [:idle_step_128, true, 128],
    [:idle_step_32, true, 32]
  ]
  GC_PHASE_UPDATES = 120
  GC_PHASE_ALLOC = 200  # objects per update
  GC_PHASE_SLEEP = 20   # ms of slack handed to _spin per update

  def start_gc_phase(index)
    row = GC_PHASE_MODES[index]
    self.idle_gc = row[1]
    GC.step_limit = row[2] if row[2]
    GC.start
    GC.reset_stat if GC.respond_to?(:reset_stat)
    @gc_phase = index
    @gc_updates = 0
    @gc_total0 = GC.stat[:total] || 0
    @gc_t0 = Machine.uptime_us
    say("#{row[0]}: #{GC_PHASE_UPDATES} updates x #{GC_PHASE_ALLOC} objects")
  end

  def burn_memory
    i = 0
    while i < GC_PHASE_ALLOC
      @garbage = [i, i]
      i += 1
    end
  end

  def report_gc_phase
    st = GC.stat
    elapsed = (Machine.uptime_us - @gc_t0) / 1000
    say("#{GC_PHASE_MODES[@gc_phase][0]} #{elapsed}ms gc=#{(st[:total] || 0) - @gc_total0}: " \
        "pause #{st[:prof_sync_count] || 0}x #{(st[:prof_sync_total_us] || 0) / 1000}ms " \
        "max #{st[:prof_sync_max_us] || 0}us")
    say("  step #{st[:prof_step_count] || 0}x #{(st[:prof_step_total_us] || 0) / 1000}ms " \
        "max #{st[:prof_step_max_us] || 0}us | " \
        "jitter #{st[:prof_step_jitter_count] || 0}x max #{st[:prof_step_jitter_max_us] || 0}us")
    # final marking cannot be split, so it is the floor under any single
    # pause however small the steps are made.
    say("  final_mark max #{st[:prof_final_mark_max_us] || 0}us at live " \
        "#{st[:prof_final_mark_max_live] || 0}, emergency #{st[:prof_emergency_count] || 0}")
  end

  # --- GC accounting -----------------------------------------------------

  # GC.stat keys, or nil when this build has neither MRB_GC_STATS nor
  # MRB_GC_PROFILE. Reading them through here keeps the bench runnable on an
  # ordinary build.
  def gc_snapshot
    st = GC.stat
    { live: st[:live] || 0,
      total: st[:total] || 0,
      major: st[:major] || 0,
      sync_count: st[:prof_sync_count] || 0,
      sync_total_us: st[:prof_sync_total_us] || 0,
      sync_max_us: st[:prof_sync_max_us] || 0 }
  end

  def gc_delta(before, after)
    "gc=#{after[:total] - before[:total]}(major #{after[:major] - before[:major]}) " \
      "pause=#{after[:sync_count] - before[:sync_count]}x " \
      "#{(after[:sync_total_us] - before[:sync_total_us]) / 1000}ms " \
      "max=#{after[:sync_max_us]}us"
  end

  def gc_report(label)
    st = gc_snapshot
    say("#{label}: live=#{st[:live]} gc=#{st[:total]} major=#{st[:major]}")
  end

  # --- 1.1 the channel scan ----------------------------------------------

  def bench_scan(data)
    player = FmrbMidi::SmfPlayer.new(MIDI::Device.new(CountingTransport.new))
    unless player.load_string(data, SONG)
      say("load failed: #{player.error}")
      return
    end

    ranges = track_ranges(data)
    say("tracks=#{ranges.size}")

    base = timed("scan gem   ") { player.channel_usage }
    with_gc_off { timed("scan gem   ", " (GC off)") { player.channel_usage } }
    fast = timed("scan cand  ") { scan_fast(data, ranges, 0) }
    with_gc_off { timed("scan cand  ", " (GC off)") { scan_fast(data, ranges, 0) } }
    timed("scan cand  ", " (600 ev/track)") { scan_fast(data, ranges, 600) }

    # The same answer, or the candidate is not a candidate.
    a = player.channel_usage
    b = scan_fast(data, ranges, 0)
    same = a.keys.sort == b.keys.sort
    a.each { |ch, stats| same = false unless b[ch] && b[ch][0] == stats[0] && b[ch][1] == stats[1] }
    say("candidate agrees: #{same} #{b.keys.sort.inspect}")

    say("on device at #{DEVICE_SIM_FACTOR}x: gem ~#{base * DEVICE_SIM_FACTOR / 1000}ms, " \
        "candidate ~#{fast * DEVICE_SIM_FACTOR / 1000}ms")
  end

  def timed(label, note = "")
    t0 = Machine.uptime_us
    g0 = gc_snapshot
    yield
    us = Machine.uptime_us - t0
    g1 = gc_snapshot
    say("#{label}#{note}: #{us}us #{gc_delta(g0, g1)}")
    us
  end

  # Separates the work from the collecting. A scan that speeds up by an order
  # of magnitude with the collector switched off is a garbage problem, not an
  # arithmetic one.
  def with_gc_off
    GC.start
    GC.disable
    begin
      yield
    ensure
      GC.enable
      GC.start
    end
  end

  def u32(data, i)
    ((data.getbyte(i) || 0) << 24) | ((data.getbyte(i + 1) || 0) << 16) |
      ((data.getbyte(i + 2) || 0) << 8) | (data.getbyte(i + 3) || 0)
  end

  # Where each MTrk chunk starts and ends. Same walk as
  # SmfPlayer#collect_tracks; repeated here so the candidate scan can run
  # without reaching into the player's privates.
  def track_ranges(data)
    ranges = []
    pos = 8 + u32(data, 4)
    size = data.length
    while pos + 8 <= size
      len = u32(data, pos + 4)
      if data.getbyte(pos) == 0x4D && data.getbyte(pos + 1) == 0x54 &&
         data.getbyte(pos + 2) == 0x72 && data.getbyte(pos + 3) == 0x6B
        ranges << [pos + 8, pos + 8 + len]
      end
      pos += 8 + len
    end
    ranges
  end

  # Candidate scan. Three differences from the gem's version, all of them
  # about not allocating: the variable-length decode is inlined (the gem's
  # returns [value, position], so every event leaves an Array behind), the
  # delta time is skipped rather than decoded (its value is not used here),
  # and the per-channel totals go into two flat arrays instead of a Hash of
  # Arrays. limit > 0 stops each track after that many events.
  def scan_fast(data, ranges, limit)
    counts = []
    sums = []
    i = 0
    while i < 16
      counts[i] = 0
      sums[i] = 0
      i += 1
    end

    ri = 0
    while ri < ranges.size
      pos = ranges[ri][0]
      finish = ranges[ri][1]
      status = 0
      seen = 0
      while pos < finish
        # Delta time: only its length matters, so walk off the end of it.
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
            sums[channel] += (data.getbyte(pos) || 0)
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
          break # cannot make sense of this track
        end

        seen += 1
        break if limit > 0 && seen >= limit
      end
      ri += 1
    end

    usage = {}
    i = 0
    while i < 16
      usage[i] = [counts[i], sums[i]] if counts[i] > 0
      i += 1
    end
    usage
  end

  # --- 1.2 playback ------------------------------------------------------

  # Plays the song as fast as tempo_scale allows and charges only the ticks
  # that actually dispatched something, so the figure is the cost of an
  # event and not of the waiting in between.
  def bench_play
    sink = CountingTransport.new
    player = FmrbMidi::SmfPlayer.new(MIDI::Device.new(sink))
    return unless player.load(SONG)

    player.tempo_scale = 100.0
    player.start
    g0 = gc_snapshot
    t0 = Machine.uptime_us
    work_us = 0
    idle_us = 0
    idle_n = 0
    # The longest tick that ran a collection, and the longest that did not.
    # If the two are far apart, the pauses in playback are the collector and
    # not the decoding -- which is the question 1.2 asks.
    worst_us = 0
    while player.playing?
      before = player.late_count
      t = Machine.uptime_us
      wait = player.tick
      dt = Machine.uptime_us - t
      break if wait.nil?

      worst_us = dt if dt > worst_us
      if player.late_count > before
        work_us += dt
      else
        idle_us += dt
        idle_n += 1
      end
      break if Machine.uptime_us - t0 > 20_000_000
    end
    events = player.late_count
    g1 = gc_snapshot

    say("play: #{events} events in #{work_us / 1000}ms " \
        "(#{events > 0 ? work_us / events : 0}us/event), sent #{sink.events}")
    say("play: idle ticks #{idle_n} #{idle_us / 1000}ms")
    say("play: worst single tick #{worst_us}us")
    say("play: #{gc_delta(g0, g1)}")
    say("play: #{player.timing_stats}")
    bench_play_alloc
  end

  # The same span of the song played twice, once with the collector free to
  # run and once with it forbidden. Same decoding, same messages: if the
  # longest tick collapses when the collector cannot run, the pauses in
  # playback are collections and nothing else.
  #
  # The run with the collector off also prices the garbage: with nothing
  # being freed, the rise in the live count over the span is exactly what one
  # event leaves behind.
  def bench_play_alloc(events = 300)
    on_worst, = play_span(events, true)
    off_worst, leaked, n = play_span(events, false)
    say("play: worst tick over #{n} events: gc on #{on_worst}us, gc off #{off_worst}us")
    say("play: #{leaked * 10 / (n > 0 ? n : 1)} objects/10 events " \
        "(#{leaked} over #{n})")
  end

  def play_span(events, collect)
    player = FmrbMidi::SmfPlayer.new(MIDI::Device.new(CountingTransport.new))
    return [0, 0, 0] unless player.load(SONG)

    player.tempo_scale = 100.0
    player.start
    GC.start
    GC.disable unless collect
    live0 = GC.stat[:live] || 0
    worst = 0
    while player.playing? && player.late_count < events
      t = Machine.uptime_us
      wait = player.tick
      dt = Machine.uptime_us - t
      break if wait.nil?

      worst = dt if dt > worst
    end
    live1 = GC.stat[:live] || 0
    n = player.late_count
    GC.enable unless collect
    player.stop
    GC.start
    [worst, live1 - live0, n]
  end

  # --- allocation per event ----------------------------------------------

  # With the collector off, the live count only goes up, so its increase is
  # exactly what one call left behind. This is the number that decides
  # whether the player can play without waking the collector at all.
  def bench_alloc
    sink = CountingTransport.new
    device = MIDI::Device.new(sink)

    measure_alloc("device.note_on ch=") { device.note_on(60, 100, channel: 3) }
    measure_alloc("device.note_on plain") { device.note_on(60, 100) }
    measure_alloc("transport.send_packet") { sink.send_packet(0, 9, 0x90, 60, 100) }
    measure_alloc("array [a, b]") { [1, 2] }
    measure_alloc("string literal") { "note_on" }

    # The APU path costs a kernel message per note. Where that message's
    # objects come from, in the order they add up: the Hash the caller
    # writes, then the serializer. A String key is two objects to store and
    # a Symbol key is none, which is why FmrbAudio#note_on uses Symbols.
    msg = { cmd: :note_on, ch: 0, freq: 440, vol: 8, duty: 2, sweep: 0 }
    strkey = { "cmd" => "note_on", "ch" => 0 }
    measure_alloc("msgpack.pack") { MessagePack.pack(msg) }
    measure_alloc("hash store, String key") { strkey["ch"] = 1 }
    measure_alloc("hash store, Symbol key") { msg[:ch] = 1 }

    check_audio_wire

    audio = FmrbAudio.new(self)
    apu = FmrbMidi::ApuTransport.new(audio)
    measure_alloc("audio.note_on", 50) { audio.note_on(0, 440, 8, 2, 0) }
    measure_alloc("apu.note_on/off", 50) do
      apu.note_on(0, 60, 100)
      apu.note_off(0, 60)
    end
    # The grouped path (P7.5) has its own bookkeeping; it must not allocate
    # either. One iteration is a three-note chord struck and released.
    measure_alloc("apu grouped chord (6 events)", 25) do
      apu.defer_voices
      apu.note_on(0, 60, 100)
      apu.note_on(0, 64, 100)
      apu.note_on(0, 67, 100)
      apu.flush_voices
      apu.defer_voices
      apu.note_off(0, 67)
      apu.note_off(0, 64)
      apu.note_off(0, 60)
      apu.flush_voices
    end

    # The same chord filed for a time instead of sent now (P7.6): this is
    # the path a song actually takes, so it is the one that has to allocate
    # nothing. Due times far enough ahead that nothing fires during the
    # measurement.
    if FmrbMidi.scheduler
      due = FmrbMidi.now_us + 60_000_000
      measure_alloc("apu scheduled chord (6 events)", 25) do
        due += 1000
        apu.defer_voices(due)
        apu.note_on(0, 60, 100)
        apu.note_on(0, 64, 100)
        apu.note_on(0, 67, 100)
        apu.flush_voices
        apu.defer_voices(due + 500)
        apu.note_off(0, 67)
        apu.note_off(0, 64)
        apu.note_off(0, 60)
        apu.flush_voices
      end
      FmrbMidi.scheduler._clear
      say("sched stats after alloc: #{FmrbMidi.scheduler._stats}")
    end

    apu.all_off
    audio.note_off(0)
    FmrbMidi.unregister(apu)
  end

  # The audio note messages are built in C now, which is only allowed because
  # the bytes are identical to what the Hash went through MessagePack.pack as.
  # This is that claim, checked rather than asserted: a value of each integer
  # width (fixint, uint8, uint16) so the encoding choices are covered too.
  def check_audio_wire
    cases = [
      [0, 440, 8, 2, 0],
      [3, 127, 127, 3, 0],      # every field still a fixint
      [9, 128, 200, 1, 0],      # crosses into uint8
      [15, 12428, 15, 2, 300]   # crosses into uint16
    ]
    bad = 0
    i = 0
    while i < cases.size
      c = cases[i]
      want = MessagePack.pack({ cmd: :note_on, ch: c[0], freq: c[1], vol: c[2],
                                duty: c[3], sweep: c[4] })
      got = _audio_note_bytes(true, c[0], c[1], c[2], c[3], c[4])
      bad += 1 unless got == want
      want_off = MessagePack.pack({ cmd: :note_off, ch: c[0] })
      got_off = _audio_note_bytes(false, c[0], 0, 0, 0, 0)
      bad += 1 unless got_off == want_off
      i += 1
    end
    say("audio wire matches MessagePack.pack: #{bad == 0} (#{cases.size * 2} cases)")
  end

  def measure_alloc(label, times = 200)
    GC.start
    GC.disable
    before = GC.stat[:live] || 0
    i = 0
    while i < times
      yield
      i += 1
    end
    after = GC.stat[:live] || 0
    GC.enable
    GC.start
    # The loop itself allocates nothing, so the difference is per call.
    say("alloc #{label}: #{(after - before) * 100 / times} objects/100 calls")
  end

  # --- screen ------------------------------------------------------------

  def draw_screen
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width,
                   @user_area_height, FmrbConst::THEME_WINDOW_BG)
    y = @user_area_y0 + 2
    i = 0
    while i < @lines.size
      @gfx.draw_text(@user_area_x0 + 2, y, @lines[i], FmrbConst::THEME_TEXT,
                     FmrbConst::THEME_WINDOW_BG)
      y += 9
      i += 1
    end
    draw_window_frame
    @gfx.present
  end
end

Log.info("MidiBenchApp.new")
begin
  app = MidiBenchApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("MidiBench script ended")

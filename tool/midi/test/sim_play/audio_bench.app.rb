# Audio path benchmark (P0-1) - how fast can Ruby drive the APU?
#
# Measures what a MIDI transport built on FmrbAudio would have to live with:
# the cost of one note_on from the app's point of view, and the rate at which
# the whole path (app VM -> kernel message -> link -> audio side) stops
# keeping up. The link is the same host message queue that GFX uses, so the
# ceiling here is shared with drawing.
#
# This app is not part of the flash image. Copy it into flash/app/demo/ for a
# run and remove it afterwards. See doc/midi/report/p0.md.

class AudioBenchApp < FmrbApp
  BURST = 200        # calls per measurement
  CHANNELS = 4       # pulse1, pulse2, triangle, noise
  NOTE_FREQS = [262, 330, 392, 8] # last one is a noise period, not a pitch

  def initialize
    super()
    @lines = ["running..."]
    @done = false
  end

  def on_create
    Log.info("AudioBench: start")
    draw
    @audio = FmrbAudio.new(self)
    # Same traffic through the MIDI layer, to see what the Ruby transport
    # adds on top of a raw FmrbAudio call (P2).
    @device = FmrbMidi.device(self)

    # MIDI::Device#pitch_bend uses Integer#clamp, which this build of mruby
    # does not have (no mruby-comparable-ext). Record what actually happens
    # so the report can state it rather than infer it.
    begin
      @device.pitch_bend(1000, channel: 0)
      Log.info("AudioBench: pitch_bend ok")
    rescue => e
      Log.info("AudioBench: pitch_bend unsupported: #{e.class}: #{e.message}")
    end
  end

  # Clicking runs the measurement again. Used to fire note_on traffic while an
  # FMSQ track is playing on the other APU instance (P0-2).
  def on_event(ev)
    super(ev)
    @done = false if ev[:type] == :mouse_up
  end

  def on_update
    return 1000 if @done

    @lines = ["AudioBench (#{BURST} calls each)"]
    run_case("note_on x1") { |i| note_on(i % 2) }
    run_case("on+off pair") do |i|
      ch = i % 2
      note_on(ch)
      @audio.note_off(ch)
    end
    run_case("chord of 3") do |_i|
      note_on(0)
      note_on(1)
      note_on(2)
    end
    run_case("midi note_on") { |i| @device.note_on(60 + (i % 12), 100, channel: i % 2) }
    run_case("midi on+off") do |i|
      note = 60 + (i % 12)
      @device.note_on(note, 100, channel: 0)
      @device.note_off(note, 0, channel: 0)
    end
    silence
    @done = true
    @lines.each { |l| Log.info("AudioBench: #{l}") }
    draw
    1000
  end

  # One measurement: BURST iterations as fast as the app can issue them.
  # Back pressure from a full queue shows up as a longer elapsed time.
  def run_case(name)
    started = Machine.board_millis
    i = 0
    while i < BURST
      yield i
      i += 1
    end
    elapsed = Machine.board_millis - started
    elapsed = 1 if elapsed <= 0
    per_call = (elapsed * 1000) / BURST
    rate = (BURST * 1000) / elapsed
    @lines << "#{name}: #{elapsed}ms #{per_call}us/call #{rate}/s"
    # Let the queue drain before the next case so they stay independent.
    Machine.delay_ms(500)
  end

  def note_on(ch)
    @audio.note_on(ch, NOTE_FREQS[ch % CHANNELS], 8, 2, 0)
  end

  def silence
    ch = 0
    while ch < CHANNELS
      @audio.note_off(ch)
      ch += 1
    end
  end

  def draw
    x0 = @user_area_x0
    y0 = @user_area_y0
    @gfx.fill_rect(x0, y0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    y = y0 + 4
    @lines.each do |line|
      @gfx.draw_text(x0 + 4, y, line, FmrbGfx::WHITE)
      y += 12
    end
    draw_window_frame
    @gfx.present
  end

  def on_destroy
    silence if @audio
    Log.info("AudioBench: done")
  end
end

Log.info("AudioBenchApp.new")
begin
  app = AudioBenchApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")

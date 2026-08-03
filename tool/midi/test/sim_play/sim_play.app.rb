# SMF playback check - loads an FMSQ produced by tool/midi/smf2fmsq.rb and
# plays it through the normal app path (sync_file -> load_fmsq_file ->
# play_slot), which is what a real user app would do.
#
# This app is not part of the flash image. Copy this directory into
# flash/app/demo/ together with the .fmsq for a sim run, then remove it:
#   ruby tool/midi/smf2fmsq.rb tool/midi/test/scale.mid -o flash/app/demo/smf_test.fmsq
#   cp tool/midi/test/sim_play/sim_play.app.* flash/app/demo/
# See doc/midi/report/p1.md.

class SmfPlayApp < FmrbApp
  APP_DIR   = "/app/demo"
  FMSQ_SRC  = "#{APP_DIR}/smf_test.fmsq"
  CACHE_DIR = "/cache/app/smf_play"
  SLOT      = 0

  def initialize
    super()
    @state = "init"
    @audio = nil
  end

  def on_create
    Log.info("SmfPlayApp: start")
    dest = "#{CACHE_DIR}/smf_test.fmsq"
    @gfx.sync_file(FMSQ_SRC, dest: dest)
    Log.info("SmfPlayApp: transferred #{FMSQ_SRC} -> #{dest}")

    @audio = FmrbAudio.new(self)
    @audio.load_fmsq_file(SLOT, dest)
    Log.info("SmfPlayApp: load_fmsq_file slot=#{SLOT}")
    @audio.play_slot(SLOT)
    Log.info("SmfPlayApp: play_slot slot=#{SLOT}")
    @state = "playing"
    draw
  rescue => e
    @state = "error: #{e.message}"
    Log.error("SmfPlayApp: #{e.class}: #{e.message}")
    draw
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up

    # Replay on click so the same path can be exercised more than once.
    if @audio
      @audio.play_slot(SLOT)
      Log.info("SmfPlayApp: replay")
      @state = "replay"
      draw
    end
  end

  def on_update
    500
  end

  def draw
    x0 = @user_area_x0
    y0 = @user_area_y0
    @gfx.fill_rect(x0, y0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    @gfx.draw_text(x0 + 4, y0 + 4, "SMF -> FMSQ playback", FmrbGfx::WHITE)
    @gfx.draw_text(x0 + 4, y0 + 20, @state, FmrbGfx::CYAN)
    @gfx.draw_text(x0 + 4, y0 + 36, "click to replay", FmrbGfx::GRAY)
    draw_window_frame
    @gfx.present
  end

  def on_destroy
    @audio.stop if @audio
    Log.info("SmfPlayApp: done")
  end
end

Log.info("SmfPlayApp.new")
begin
  app = SmfPlayApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")

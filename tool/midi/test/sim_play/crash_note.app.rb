# Deliberately crashes while a note is sounding, to check that the kernel
# releases the APU voice for an app that never reaches on_destroy
# (doc/midi/report/p4.md, the P2 follow-up).
#
# Not part of the flash image: copy into flash/app/demo/ for a test run.

class CrashNoteApp < FmrbApp
  def on_create
    @audio = FmrbAudio.new(self)
    @audio.note_on(0, 440, 12, 2, 0)
    Log.info("CrashNote: note on; crashing next update")
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, 0x00)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, "crashing...", 0xFF)
    draw_window_frame
    @gfx.present
  end

  def on_update
    raise "intentional crash while a note is sounding"
  end
end

Log.info("CrashNoteApp.new")
begin
  app = CrashNoteApp.new
  app.start
rescue => e
  Log.error("Exception: #{e.class}: #{e.message}")
end
Log.info("Script ended")

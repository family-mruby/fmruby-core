# Motion-JPEG player demo (Modern / Tab5 only).
#
# Plays /sd/movie/demo.mjpg -- a file of JPEG frames written back to back --
# inside this window. The picture is drawn by the display side; this app only
# lays out the window, starts the player, and offers the buttons.
#
# Make a file with:
#   ffmpeg -i source.mp4 -vf "scale=320:176,fps=15" -q:v 4 -f mjpeg demo.mjpg
#
# Keep both dimensions a multiple of 16: that is the grid the decoder works
# on, and it is what lets the frame land in the canvas without a reshuffle.
# The screen is 426x240, and this window keeps a row of buttons and a status
# line above the picture, so 320x176 is what still fits.

class VideoPlayApp < FmrbApp
  MOVIE_PATH = "/sd/movie/demo.mjpg"
  FPS = 15

  BTN_Y = 4
  BTN_W = 44
  BTN_H = 10

  # The picture goes under the buttons and the status line. Those are drawn in
  # canvas coordinates from the user area, so the picture has to start from the
  # same origin -- a constant offset would land on top of them, and the player
  # would then paint over the buttons every frame.
  VIDEO_DX = 4
  VIDEO_DY = BTN_Y + BTN_H + 1 + 8 + 2

  def on_create
    @video = nil
    @message = nil
    @last_status_ms = 0

    draw_screen
    open_video
  end

  def open_video
    @video = @gfx.video_open(MOVIE_PATH,
                             x: @user_area_x0 + VIDEO_DX,
                             y: @user_area_y0 + VIDEO_DY,
                             fps: FPS, loop: true)
    if @video.nil?
      @message = "no video: #{MOVIE_PATH}"
      draw_screen
      return
    end
    @video.play
    @message = "#{@video.width}x#{@video.height} @#{FPS}"
    draw_status
  end

  def draw_screen
    clear_user_area
    draw_button(0, "Play")
    draw_button(1, "Pause")
    draw_button(2, "Rewind")
    draw_status
    draw_window_frame
    @gfx.present
  end

  # Buttons sit in a row along the top, above the picture.
  def button_x(index)
    @user_area_x0 + 4 + index * (BTN_W + 4)
  end

  def draw_button(index, label)
    x = button_x(index)
    y = @user_area_y0 + BTN_Y
    @gfx.fill_rect(x, y, BTN_W, BTN_H, FmrbGfx::BLUE)
    @gfx.draw_rect(x, y, BTN_W, BTN_H, FmrbGfx::WHITE)
    lx = x + (BTN_W - label.length * 6) / 2
    @gfx.draw_text(lx, y + 1, label, FmrbGfx::WHITE, FmrbGfx::BLUE)
  end

  def hit_button?(ev, index)
    x = button_x(index)
    y = @user_area_y0 + BTN_Y
    ev[:x] >= x && ev[:x] < x + BTN_W && ev[:y] >= y && ev[:y] < y + BTN_H
  end

  # One status line under the buttons. Kept outside the picture rect so the
  # player never overwrites it.
  def draw_status
    y = @user_area_y0 + BTN_Y + BTN_H + 1
    w = @user_area_width - 8
    @gfx.fill_rect(@user_area_x0 + 4, y, w, 8, FmrbGfx::BLACK)
    text = @message || ""
    if @video
      st = @video.status
      if st
        text = "#{@message}  shown:#{st[:shown]} drop:#{st[:dropped]}"
      end
    end
    @gfx.draw_text(@user_area_x0 + 4, y, text, FmrbGfx::WHITE)
    @gfx.present
  end

  def on_event(ev)
    super(ev)
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    return if @video.nil?

    if hit_button?(ev, 0)
      @video.play
    elsif hit_button?(ev, 1)
      @video.pause
    elsif hit_button?(ev, 2)
      @video.rewind
    else
      return
    end
    draw_status
  end

  # The picture arrives on its own; this only refreshes the counters, and
  # rarely, so the status line does not fight the player for the canvas.
  def on_update
    now = Machine.board_millis
    if @video && (now - @last_status_ms) >= 1000
      @last_status_ms = now
      draw_status
    end
    200
  end

  def on_destroy
    @video.stop if @video
  end
end

begin
  app = VideoPlayApp.new
  app.start
rescue => e
  Log.error("VideoPlayApp: #{e}")
end

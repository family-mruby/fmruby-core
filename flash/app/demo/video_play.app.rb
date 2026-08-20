# Motion-JPEG player demo (Modern / Tab5 only).
#
# Plays a file of JPEG frames written back to back inside this window. The
# picture is drawn by the display side; this app only lays out the window,
# starts the player, and offers the buttons. It opens /sd/movie/demo.mjpg on
# startup if that file is there, and "Open" picks any other file with the
# system file selector (the SD card is under /mnt/sd there).
#
# Make a file with:
#   ffmpeg -i source.mp4 -vf "scale=320:176,fps=15" -q:v 4 -f mjpeg demo.mjpg
#
# Keep both dimensions a multiple of 16: that is the grid the decoder works
# on, and it is what lets the frame land in the canvas without a reshuffle.
# The screen is 426x240, and this window keeps a row of buttons and a status
# line above the picture, so 320x176 is what still fits.
#
# The file says nothing about its own frame rate, so the rate is this app's to
# choose: the "15fps" button steps through 1, 5, 10, 15, 20 and 30, and the
# right button steps back. Playing a 30fps file at 15 is slow motion, not an
# error; 1fps is a way to look at a file frame by frame.

class VideoPlayApp < FmrbApp
  MOVIE_PATH = "/sd/movie/demo.mjpg"
  RATES = [1, 5, 10, 15, 20, 30]
  DEFAULT_RATE_INDEX = 3   # 15fps, what the demo file is made at

  BTN_Y = 4
  BTN_W = 44
  BTN_H = 10
  BTN_PLAY = 0
  BTN_PAUSE = 1
  BTN_REWIND = 2
  BTN_OPEN = 3
  BTN_RATE = 4

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
    @path = MOVIE_PATH
    @rate_index = DEFAULT_RATE_INDEX

    draw_screen
    open_video(@path)
  end

  def fps
    RATES[@rate_index]
  end

  # Drop the player and wipe the rectangle it was drawing into. Without the
  # wipe the last frame of the previous file stays on the canvas, and a
  # smaller new picture would leave a frame of it around the edges.
  def close_video
    if @video
      @video.stop
      @video = nil
    end
    @gfx.fill_rect(@user_area_x0 + VIDEO_DX, @user_area_y0 + VIDEO_DY,
                   @user_area_width - VIDEO_DX * 2,
                   @user_area_height - VIDEO_DY - 2, FmrbGfx::BLACK)
  end

  def open_video(path)
    close_video
    @path = path
    @video = @gfx.video_open(path,
                             x: @user_area_x0 + VIDEO_DX,
                             y: @user_area_y0 + VIDEO_DY,
                             fps: fps, loop: true)
    if @video.nil?
      # Modern-only, and the file has to be a readable JPEG sequence.
      @message = "cannot play #{basename(path)}"
      draw_screen
      return
    end
    @video.play
    @message = "#{basename(path)} #{@video.width}x#{@video.height}"
    draw_screen
  end

  def basename(path)
    path.split("/").last || path
  end

  # The file selector belongs to the desktop, so the answer comes back as a
  # message rather than as a return value.
  def on_control(msg)
    return unless msg["cmd"] == "file_selected"

    path = msg["path"]
    if path.nil?
      @message = "no file picked"
      draw_status
      return
    end
    open_video(path)
  end

  def draw_screen
    clear_user_area
    draw_button(BTN_PLAY, "Play")
    draw_button(BTN_PAUSE, "Pause")
    draw_button(BTN_REWIND, "Rewind")
    draw_button(BTN_OPEN, "Open")
    draw_button(BTN_RATE, "#{fps}fps")
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
    return unless ev[:type] == :mouse_up
    return unless ev[:button] == 1 || ev[:button] == 3

    # Six rates is a long way round with one button, so the right button walks
    # back. Everything else answers to the left button only.
    if hit_button?(ev, BTN_RATE)
      step = (ev[:button] == 3) ? -1 : 1
      # + length first: a negative left operand of % is not somewhere to rely
      # on picoruby matching Ruby.
      @rate_index = (@rate_index + step + RATES.length) % RATES.length
      draw_button(BTN_RATE, "#{fps}fps")
      # The rate is fixed when the file is opened, so reopen to apply it.
      open_video(@path) if @video
      return
    end

    return unless ev[:button] == 1

    # Open works with no file loaded; the transport buttons do not.
    if hit_button?(ev, BTN_OPEN)
      @message = "pick a file (SD is under /mnt/sd)"
      draw_status
      request_file_select("open")
      return
    end

    return if @video.nil?

    if hit_button?(ev, BTN_PLAY)
      @video.play
    elsif hit_button?(ev, BTN_PAUSE)
      @video.pause
    elsif hit_button?(ev, BTN_REWIND)
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

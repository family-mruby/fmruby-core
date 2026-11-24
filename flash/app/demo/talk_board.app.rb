#---fmrb
# app_screen_name = "Talk Board"
# app_screen_name_ja = "伝言板"
# default_window_mode = "window"
# default_window_width = 240
# default_window_height = 120
#---
# Talk board: the side that exposes an object (Asterism, sig/fmrb_asterism.rbs).
# Other machines call post(text) on <this node>/talk_board/board, and every
# post goes out on the topic <this node>/talk_board/posts to whoever
# subscribes (talk_client.app.rb is the other side).
class TalkBoardApp < FmrbApp
  APP = "talk_board"
  MAX_TEXT = 60

  def on_create
    @gfx.set_font(:ja, 12)
    @lines = []
    @posts = nil
    @state = "つないでいます…"
    @link = FmrbAsterism::Link.new(self, APP)
    draw_screen
  end

  # The Link calls this after every connect: expose and publish again on
  # the new connection.
  def link_up(link)
    link.conn.expose("board", self, methods: { post: 1 })
    @posts = link.conn.publish("posts")
    @state = "#{link.node}/#{APP}/board"
    draw_screen
  end

  def link_down(link)
    @posts = nil
    @state = "つながりません: #{link.reason}"
    draw_screen
  end

  # Called from another machine (inside @link.update). What it raises goes
  # back to the caller as an Asterism::RemoteError.
  def post(text)
    text = text.to_s
    raise ArgumentError, "empty text" if text.empty?
    raise ArgumentError, "longer than #{MAX_TEXT}" if text.length > MAX_TEXT
    who = @link.conn.current_call.from
    @posts.put({ "from" => who, "text" => text }) if @posts
    @lines << "#{who}: #{text}"
    @lines.shift while @lines.size > 6
    @dirty = true
    @lines.size
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(4, 4, @gfx.fit_text(@state, @user_area_width - 8, ".."), theme_accent)
    y = 22
    @lines.each do |l|
      @gfx.draw_text(4, y, @gfx.fit_text(l, @user_area_width - 8, ".."), theme_fg)
      y += 14
    end
    @gfx.present
    @dirty = false
  end

  def on_update
    @link.update
    draw_screen if @dirty
    100
  end

  def on_destroy
    @link.close
  end
end

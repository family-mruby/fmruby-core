#---fmrb
# app_screen_name = "Talk"
# app_screen_name_ja = "伝言"
# default_window_mode = "window"
# default_window_width = 240
# default_window_height = 130
#---
# Talk: the side that calls another machine (Asterism, sig/fmrb_asterism.rbs).
# Finds a talk board (talk_board.app.rb) on the network, sends what is typed
# with board.post(text) without waiting (async), and shows every post the
# boards put on their topic "posts".
class TalkClientApp < FmrbApp
  APP = "talk"

  def on_create
    @gfx.set_font(:ja, 12)
    @lines = []
    @call = nil
    @state = "つないでいます…"
    @ui = FmrbUI.new(self)
    @ui.text_field(:text, 4, 4, 180, 14, "", max: 60)
    @ui.button(:send, 188, 4, 46, 14, "送る")
    @link = FmrbAsterism::Link.new(self, APP)
    draw_screen
  end

  # After every connect: subscribe again on the new connection.
  def link_up(link)
    link.conn.subscribe("*/talk_board/posts") { |v, info| got(v) }
    @state = "つながりました (#{link.node})"
    @dirty = true
  end

  def link_down(link)
    @state = "つながりません: #{link.reason}"
    @dirty = true
  end

  # A post on a board's topic (runs inside @link.update).
  def got(v)
    note("#{v["from"]}: #{v["text"]}")
  end

  def note(text)
    @lines << text
    @lines.shift while @lines.size > 5
    @dirty = true
  end

  def send_text
    text = @ui.field_text(:text).to_s
    return if text.empty? || @call
    return note("つながっていません") unless @link.connected?
    boards = @link.conn.each("*/talk_board/board")
    return note("伝言板が見つかりません") if boards.empty?
    @call = boards[0].async.post(text)
    @ui.set_field_text(:text, "")
  rescue ::Asterism::Error => e
    note("送れません: #{e.message}")
  end

  # The answer of the call, once it is there.
  def check_call
    f = @call
    return unless f && f.done?
    @call = nil
    begin
      n = f.value
      @state = "送りました (#{n} 件目、#{f.took_ms} ms)"
    rescue ::Asterism::RemoteError => e
      @state = "断られました: #{e.remote_message}"
    rescue ::Asterism::Error => e
      @state = "届きません: #{e.message}"
    end
    @dirty = true
  end

  def on_event(ev)
    case @ui.handle(ev)
    when :text, :send then send_text
    when nil then return @ui.flush
    end
    draw_screen
  end

  def draw_screen
    clear_user_area
    @gfx.draw_text(4, 24, @gfx.fit_text(@state, @user_area_width - 8, ".."), theme_accent)
    y = 42
    @lines.each do |l|
      @gfx.draw_text(4, y, @gfx.fit_text(l, @user_area_width - 8, ".."), theme_fg)
      y += 14
    end
    @ui.flush
    @dirty = false
  end

  def on_update
    check_call if @link.update
    draw_screen if @dirty
    100
  end

  def on_destroy
    @link.close
  end
end

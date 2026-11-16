# The owner's question about an app another machine sent to be installed
# (doc/wasm_apps W6). The transfer app (/usr/share/transfer/transfer.app.rb)
# has unpacked the app beside /app/usr and looked at it (a wasm app imports
# only what this firmware gives); it starts this app with a ticket (the
# kernel's open_path: a file_selected message), one "key<TAB>value" a line:
#
#   id, from (the sender's node), name (the folder in /app/usr), label (its
#   name on screen), lang, bytes, files, sdk (a wasm app's SDK version, ""
#   when none), replaces ("1": an app of that name is installed here and is
#   replaced)
#
# Install or Decline goes back to the transfer app over Pub/Sub
# ("transfer/ctl" {"cmd" => "install_answer", "id", "yes"}), which takes it
# from this app's PID only; then this app ends. Closing the window (or no
# answer in the transfer app's time, when it stops this app) is a Decline.

class InstallAskApp < FmrbApp
  TOPIC = "transfer/ctl"
  LINE_H = 11
  # the transfer app's wait (TransferInbox::ASK_MS), shown as a countdown
  ASK_S = 60

  def on_create
    FmrbI18n.add(
      "en" => { ia_title: "Install this app?", ia_from: "From", ia_app: "App", ia_lang: "Language",
                ia_size: "Size", ia_sdk: "SDK", ia_replaces: "Replaces the app of that name here.",
                ia_install: "Install", ia_decline: "Decline", ia_wait: "Reading...", ia_left: "s left",
                ia_files: "files" },
      "ja" => { ia_title: "このアプリを入れますか", ia_from: "送り手", ia_app: "アプリ", ia_lang: "言語",
                ia_size: "大きさ", ia_sdk: "SDK", ia_replaces: "同じ名前の入っているアプリを置き換える。",
                ia_install: "入れる", ia_decline: "断る", ia_wait: "読み込み中...", ia_left: "秒",
                ia_files: "ファイル" }
    )
    @t = nil
    @answered = false
    @t0 = Machine.board_millis
    @shown_s = -1
    @ui = FmrbUI.new(self)
    by = @user_area_height - 18
    @ui.button(:yes, @user_area_width - 156, by, 72, 15, FmrbI18n.t(:ia_install), accent: 0x34)
    @ui.button(:no, @user_area_width - 78, by, 72, 15, FmrbI18n.t(:ia_decline))
    draw_screen
  end

  def on_control(msg)
    return unless msg.is_a?(Hash) && msg["cmd"] == "file_selected" && msg["path"]
    @t = read_ticket(msg["path"].to_s)
    draw_screen
  end

  def read_ticket(path)
    text = File.open(path, "r") { |f| f.read }.to_s
    out = {}
    lines = text.split("\n")
    i = 0
    while i < lines.size
      tab = lines[i].index("\t")
      out[lines[i][0, tab]] = lines[i][tab + 1, lines[i].length - tab - 1].to_s if tab
      i += 1
    end
    out
  rescue => e
    Log.warn("install_ask: #{path}: #{e.message}")
    nil
  end

  def draw_screen
    clear_user_area
    w = @user_area_width
    t = @t
    if t.nil?
      @gfx.draw_text_mixed(6, 6, FmrbI18n.t(:ia_wait), theme_fg)
    else
      y = 4
      @gfx.draw_text_mixed(6, y, FmrbI18n.t(:ia_title), theme_fg)
      y += LINE_H + 3
      row(y, :ia_from, t["from"].to_s)
      y += LINE_H
      row(y, :ia_app, "#{t['label']} (#{t['name']})")
      y += LINE_H
      row(y, :ia_lang, t["lang"].to_s)
      y += LINE_H
      row(y, :ia_size, "#{size_text(t['bytes'].to_i)}, #{t['files']} #{FmrbI18n.t(:ia_files)}")
      y += LINE_H
      unless t["sdk"].to_s.empty?
        row(y, :ia_sdk, t["sdk"].to_s)
        y += LINE_H
      end
      if t["replaces"] == "1"
        @gfx.draw_text_mixed(6, y, @gfx.fit_text(FmrbI18n.t(:ia_replaces), w - 12, "..", nil, :default),
                             theme_border)
      end
    end
    draw_left
    @ui.flush
    @gfx.present
  end

  def row(y, key, value)
    @gfx.draw_text_mixed(6, y, FmrbI18n.t(key), theme_border)
    @gfx.draw_text_mixed(62, y, @gfx.fit_text(value, @user_area_width - 68, "..", nil, :default), theme_fg)
  end

  def size_text(n)
    return "#{n} B" if n < 1024
    "#{(n + 512) / 1024} KB"
  end

  # The seconds left to answer, beside the buttons.
  def draw_left
    left = ASK_S - (Machine.board_millis - @t0) / 1000
    left = 0 if left < 0
    @shown_s = left
    y = @user_area_height - 15
    @gfx.fill_rect(4, y, 60, 9, theme_bg)
    @gfx.draw_text_mixed(6, y, "#{left} #{FmrbI18n.t(:ia_left)}", theme_border)
  end

  def answer(yes)
    return if @answered || @t.nil?
    @answered = true
    publish(TOPIC, { "cmd" => "install_answer", "id" => @t["id"].to_i, "yes" => yes })
    Log.info("install_ask: #{@t['name']} from #{@t['from']}: #{yes ? 'install' : 'decline'}")
    stop
  end

  def on_event(ev)
    id = @ui.handle(ev)
    if id == :yes
      answer(true)
    elsif id == :no
      answer(false)
    elsif ev[:type] == :key_down && @t && ev[:scancode] == FmrbConst::KEY_ESC
      answer(false)
    end
    @ui.flush unless @answered
  end

  def on_update
    left = ASK_S - (Machine.board_millis - @t0) / 1000
    if left != @shown_s && !@answered
      draw_left
      @gfx.present
    end
    250
  end
end

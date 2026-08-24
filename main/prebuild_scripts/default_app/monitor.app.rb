# System Monitor
#   Page 1: heap usage color bars (memory)
#   Page 2: GFX stats time-series graphs (cmds/s, presents/s) over last 30s
#   Page 3: task list, with [X] to end a user task (two clicks; the kernel
#           asks the app to end itself -- nothing is forced)
#
# Click the "<" / ">" arrows in the bottom nav bar to switch pages.
# Polls FmrbApp.gfx_stats once per second; caller computes rate from deltas.

class MonitorApp < FmrbApp
  UPDATE_MS = 1000
  BAR_HEIGHT = 10
  BAR_PAD = 2
  CHAR_W = 6
  CHAR_H = 8
  NAV_H = 10          # bottom nav bar height (incl. padding)
  ARROW_HIT_W = 16    # click area width for < and >
  HIST_LEN = 30       # 30 samples = 30 seconds at 1 Hz
  NUM_PAGES = 3
  TYPE_USER = 2       # FmrbApp.ps :type of a user task (the only killable one)
  ROW_H = 9           # task row pitch on page 3
  KILL_BTN_W = 3 * CHAR_W + 2
  # One kill button per possible task, built once in on_create and shown or
  # hidden as the list changes -- rows must never build widgets, or the page
  # would allocate every second. FMRB_MAX_APPS (9) is not exported to Ruby;
  # it is also more rows than fit in this window, so the visible count is
  # bounded by the height as well.
  KILL_IDS = [:kill0, :kill1, :kill2, :kill3, :kill4,
              :kill5, :kill6, :kill7, :kill8].freeze

  # Services live inside the host task and have no pid, so they appear as
  # child rows under its row with a button of their own. Built once like the
  # kill buttons, and for the same reason.
  SVC_IDS = [:svc0, :svc1, :svc2, :svc3, :svc4, :svc5].freeze
  # The host's task name, and where requests and answers go.
  SVC_HOST_NAME = "Services"
  SVC_CTL_TOPIC = "svc/ctl"
  # "[start]" is the widest label, and it fixes the column: with the monitor's
  # 180px window that leaves 21 characters for the row text, which is why the
  # child rows are indented by one character and not three.
  SVC_BTN_W = 7 * CHAR_W + 2

  # Page colours from the system theme ([theme] in system_conf.toml): the
  # monitor is a tool window, so it reads like the rest of the desktop.
  COLOR_BG     = FmrbConst::THEME_WINDOW_BG
  COLOR_TEXT   = FmrbConst::THEME_TEXT
  COLOR_DIM    = FmrbConst::THEME_BORDER
  COLOR_BORDER = FmrbConst::THEME_BORDER
  COLOR_NAV_HI = FmrbConst::THEME_TEXT

  # Bar colors (page 1): saturated blocks, white figures on top.
  COLOR_FREE = FmrbGfx.rgb_to_332(60, 180, 60)
  COLOR_USED = FmrbGfx.rgb_to_332(220, 60, 60)

  # Graph colors (page 2): dark enough to read as lines on a pale page.
  COLOR_GRAPH_CMDS = FmrbGfx.rgb_to_332(0, 90, 200)
  COLOR_GRAPH_PRES = FmrbGfx.rgb_to_332(200, 100, 0)
  COLOR_AXIS       = FmrbConst::THEME_BORDER

  def initialize
    super()
    @frame_ms = UPDATE_MS
  end

  def on_create
    # Layout
    @content_x = @user_area_x0 + 2
    @content_w = @user_area_width - 4
    @label_chars = 8
    @label_w = @label_chars * CHAR_W + 2
    @bar_x = @content_x + @label_w
    @bar_w = @content_w - @label_w
    @nav_y = @user_area_y0 + @user_area_height - NAV_H

    # Pagination + sampling state
    @page = 0
    @hist_cmds = []
    @hist_presents = []
    stats = FmrbApp.gfx_stats
    @prev_cum_cmds = stats[:cmds] || 0
    @prev_cum_presents = stats[:presents] || 0
    @cur_cmds_per_sec = 0
    @cur_presents_per_sec = 0

    # Task page state: the pid each button row currently stands for, the row
    # a first click armed, and the last word from the kernel. @task_sig is
    # what the page looked like when it was last drawn; while it is unchanged
    # the page paints nothing at all.
    @kill_btn_x = @content_x + @content_w - KILL_BTN_W
    @kill_armed_pid = nil
    @kill_msg = nil
    @task_sig = nil
    @row_pids = []

    # Services, as the host last described them. @svc_pending collects the
    # rows of an answer in flight; they replace @svcs when its end arrives, so
    # a half-delivered list is never drawn.
    @svcs = []
    @svc_pending = []
    @svc_names = []
    @svc_subscribed = false

    build_kill_buttons
    draw_all
  end

  # Rows start below the "Tasks" heading and stop above the message line.
  # Coordinates handed to FmrbUI are user-area relative; everything else in
  # this file is absolute, hence the subtraction.
  def build_kill_buttons
    @ui = FmrbUI.new(self, bg: COLOR_BG)
    @rows_y0 = @user_area_y0 + 2 + CHAR_H + 3
    msg_y = @nav_y - CHAR_H - 1
    fit = (msg_y - 1 - @rows_y0) / ROW_H
    fit = KILL_IDS.size if fit > KILL_IDS.size
    fit = 0 if fit < 0
    @max_rows = fit
    rel_x = @kill_btn_x - @user_area_x0
    i = 0
    while i < @max_rows
      rel_y = @rows_y0 + i * ROW_H - @user_area_y0
      b = @ui.button(KILL_IDS[i], rel_x, rel_y, KILL_BTN_W, ROW_H, "[X]")
      b.visible = false
      b.dirty = false
      @row_pids << nil
      i += 1
    end
    svc_x = @content_x + @content_w - SVC_BTN_W - @user_area_x0
    i = 0
    while i < SVC_IDS.size
      b = @ui.button(SVC_IDS[i], svc_x, @rows_y0 - @user_area_y0, SVC_BTN_W,
                     ROW_H, "[stop]")
      b.visible = false
      b.dirty = false
      @svc_names << nil
      i += 1
    end
    nil
  end

  def on_update
    sample_gfx_stats
    # Pages 1 and 2 are live graphs and redraw every tick by definition. The
    # task list normally does not change, so it only repaints when it did.
    if @page == 2
      refresh_tasks
    else
      draw_all
    end
    @frame_ms
  end

  def on_event(ev)
    super(ev)
    if @page == 2
      id = @ui.handle(ev)
      if id
        if KILL_IDS.index(id)
          handle_kill_click(id)
        else
          handle_svc_click(id)
        end
        return
      end
      @ui.flush
    end
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    y = ev[:y]
    handle_nav_click(ev[:x]) if y >= @nav_y && y < @nav_y + NAV_H
  end

  # The kernel's answer to a kill request (see request_kill), and the service
  # host's answers, which arrive as Pub/Sub deliveries on this app's own reply
  # topic.
  def on_control(msg)
    cmd = msg["cmd"]
    if cmd == "topic_data" && msg["topic"] == svc_reply_topic
      handle_svc_reply(msg["data"] || {})
      return
    end
    return unless cmd == "kill_result"
    pid = msg["pid"]
    if msg["ok"]
      @kill_msg = "asked #{msg["info"]} (PID #{pid})"
    else
      @kill_msg = "PID #{pid}: #{msg["info"]}"
    end
    refresh_tasks if @page == 2
  end

  # ---- services ----------------------------------------------------------
  #
  # The list is asked for when the page is opened and after a button, not on
  # the once-a-second tick: the page is a task list first, and a request per
  # second would put traffic through the kernel for a window nobody is
  # looking at most of the time.

  def svc_reply_topic
    @svc_reply_topic = "svc/re/#{FmrbApp.pid}" unless @svc_reply_topic
    @svc_reply_topic
  end

  def svc_host_running?
    procs = FmrbApp.ps
    return false unless procs
    i = 0
    while i < procs.size
      p = procs[i]
      return true if p[:name] == SVC_HOST_NAME && p[:state] != 0
      i += 1
    end
    false
  end

  # No host means no child rows and no complaint: a machine without services
  # gets the task page exactly as it was before they existed.
  def request_svc(cmd, name)
    unless svc_host_running?
      @svcs = []
      @svc_pending = []
      return nil
    end
    unless @svc_subscribed
      subscribe(svc_reply_topic)
      @svc_subscribed = true
    end
    req = { "cmd" => cmd, "reply_to" => svc_reply_topic }
    req["name"] = name if name
    publish(SVC_CTL_TOPIC, req)
    nil
  end

  def handle_svc_reply(data)
    case data["cmd"]
    when "svc"
      row = data["svc"]
      @svc_pending << row if row
    when "svc_end"
      @svcs = @svc_pending
      @svc_pending = []
      @task_sig = nil          # the rows changed; let the page repaint
      refresh_tasks if @page == 2
    when "svc_result"
      @kill_msg = data["ok"] ? "#{data["name"]}: ok" : "#{data["name"]}: #{data["err"]}"
      # Ask again rather than guessing the new state from the answer.
      request_svc("list", nil)
    end
    nil
  end

  # One button per row, and which one it is depends on the state: a running
  # service can be stopped, anything else can be started. enable / disable
  # are deliberately not here -- they change what happens at the next boot,
  # which is a decision for the shell, not for a click in a monitor window.
  def handle_svc_click(id)
    i = SVC_IDS.index(id)
    return if i.nil?
    name = @svc_names[i]
    return if name.nil?
    state = svc_state_of(name)
    return if state == "disabled"
    request_svc(state == "running" ? "stop" : "start", name)
    nil
  end

  def svc_state_of(name)
    i = 0
    while i < @svcs.size
      row = @svcs[i]
      return row["state"].to_s if row["name"].to_s == name
      i += 1
    end
    ""
  end

  def on_destroy
    unsubscribe(svc_reply_topic) if @svc_subscribed
    Log.info("Monitor destroyed")
  end

  private

  def handle_nav_click(x)
    if x >= @content_x && x < @content_x + ARROW_HIT_W
      turn_page((@page - 1 + NUM_PAGES) % NUM_PAGES)
    elsif x >= @content_x + @content_w - ARROW_HIT_W && x < @content_x + @content_w
      turn_page((@page + 1) % NUM_PAGES)
    end
    nil
  end

  # Hide the kill buttons before the new page is painted, not after: a hidden
  # widget is repainted with the background on the next flush, which would
  # otherwise punch holes in whatever page 1 or 2 just drew.
  def turn_page(page)
    hide_kill_buttons
    @ui.flush
    @page = page
    @kill_armed_pid = nil
    @task_sig = nil
    draw_all
    if @page == 2
      request_svc("list", nil)
      refresh_tasks
    end
    nil
  end

  def hide_kill_buttons
    i = 0
    while i < @max_rows
      @ui.set_visible(KILL_IDS[i], false)
      @row_pids[i] = nil
      i += 1
    end
    i = 0
    while i < SVC_IDS.size
      @ui.set_visible(SVC_IDS[i], false)
      @svc_names[i] = nil
      i += 1
    end
    nil
  end

  # Two clicks on [X], not one: this window shows memory bars on page 1 and a
  # stray click a second after a page turn must not end an app. The first click
  # arms the row (the button reads [?]), the second sends the request.
  def handle_kill_click(id)
    i = KILL_IDS.index(id)
    return if i.nil?
    pid = @row_pids[i]
    return if pid.nil?
    if @kill_armed_pid == pid
      request_kill(pid)
      @kill_armed_pid = nil
    else
      @kill_armed_pid = pid
      @kill_msg = "click again to end PID #{pid}"
    end
    refresh_tasks
    nil
  end

  # The kernel does the asking: it knows who requested it, and it refuses
  # anything that is not a user task, plus the requester itself. Nothing here
  # forces a task away -- an app stuck in its own loop stays.
  def request_kill(pid)
    @kill_msg = "asking PID #{pid} ..."
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                 { "cmd" => "kill", "pid" => pid })
  end

  def sample_gfx_stats
    stats = FmrbApp.gfx_stats
    cur_cmds = stats[:cmds] || 0
    cur_presents = stats[:presents] || 0
    # Signed subtraction. If the counter wrapped the signed mrb_int range
    # between samples (weeks of uptime), we clamp to 0 for that single sample.
    dc = cur_cmds - @prev_cum_cmds
    dc = 0 if dc < 0
    dp = cur_presents - @prev_cum_presents
    dp = 0 if dp < 0
    @prev_cum_cmds = cur_cmds
    @prev_cum_presents = cur_presents
    @cur_cmds_per_sec = dc
    @cur_presents_per_sec = dp
    @hist_cmds << dc
    @hist_cmds.shift if @hist_cmds.length > HIST_LEN
    @hist_presents << dp
    @hist_presents.shift if @hist_presents.length > HIST_LEN
  end

  def draw_all
    clear_user_area(COLOR_BG)
    if @page == 0
      draw_page_memory
    elsif @page == 1
      draw_page_gfx_stats
    else
      draw_tasks_heading
    end
    draw_nav
    draw_window_frame
    @gfx.present
  end

  def draw_page_memory
    y = @user_area_y0 + 2
    @gfx.draw_text(@content_x, y, "Memory", COLOR_TEXT, COLOR_BG)
    y += CHAR_H + 4

    # IRAM line above the per-task PSRAM pool bars.
    hi = FmrbApp.heap_info
    if hi
      iram_free = hi[:iram_free] || 0
      iram_total = hi[:iram_total] || 0
      if iram_total > 0
        iram_text = "IRAM free: #{iram_free / 1024}/#{iram_total / 1024}K"
      else
        iram_text = "IRAM free: #{iram_free / 1024}K"
      end
      @gfx.draw_text(@content_x, y, iram_text, COLOR_TEXT, COLOR_BG)
      y += CHAR_H + 2
    end

    procs = FmrbApp.ps
    return unless procs

    procs.each do |p|
      break if y + BAR_HEIGHT + BAR_PAD > @nav_y - 2

      total = p[:mem_total] || 0
      next if total == 0

      used = p[:mem_used] || 0
      name = p[:name] || "?"
      state = p[:state]

      name = name[0, @label_chars] if name.length > @label_chars

      label_color = state == 2 ? COLOR_TEXT : COLOR_DIM
      @gfx.draw_text(@content_x, y + 1, name, label_color, COLOR_BG)

      inner_w = @bar_w - 2
      @gfx.fill_rect(@bar_x, y, @bar_w, BAR_HEIGHT, COLOR_BORDER)
      @gfx.fill_rect(@bar_x + 1, y + 1, inner_w, BAR_HEIGHT - 2, COLOR_FREE)

      if total > 0 && used > 0
        used_w = (inner_w * used) / total
        used_w = 1 if used_w < 1
        used_w = inner_w if used_w > inner_w
        @gfx.fill_rect(@bar_x + 1, y + 1, used_w, BAR_HEIGHT - 2, COLOR_USED)
      end

      pct = total > 0 ? (used * 100) / total : 0
      pct_text = "#{used / 1024}/#{total / 1024}K #{pct}%"
      if pct_text.length * CHAR_W > inner_w
        pct_text = "#{pct}%"
      end
      text_x = @bar_x + (@bar_w - pct_text.length * CHAR_W) / 2
      @gfx.draw_text(text_x, y + 1, pct_text, FmrbGfx::WHITE)

      y += BAR_HEIGHT + BAR_PAD
    end
  end

  def draw_page_gfx_stats
    y = @user_area_y0 + 2
    @gfx.draw_text(@content_x, y, "GFX Stats", COLOR_TEXT, COLOR_BG)
    y += CHAR_H + 4

    content_top = y
    content_bot = @nav_y - 2
    avail_h = content_bot - content_top
    gap = 4
    graph_h = (avail_h - gap) / 2

    draw_graph(content_top, graph_h,
               "cmds/s", @cur_cmds_per_sec, @hist_cmds, COLOR_GRAPH_CMDS)
    draw_graph(content_top + graph_h + gap, graph_h,
               "prs/s", @cur_presents_per_sec, @hist_presents, COLOR_GRAPH_PRES)
  end

  # Single stacked graph: header line "<label> <cur> max=<peak>", then a
  # line chart scaled to the peak of the current history window.
  def draw_graph(y0, h, label, current, hist, color)
    peak = array_max(hist)
    peak = 1 if peak < 1
    header = "#{label} #{current} max=#{peak}"
    @gfx.draw_text(@content_x, y0, header, COLOR_TEXT, COLOR_BG)

    gy_top = y0 + CHAR_H + 1
    gy_bot = y0 + h - 1
    gh = gy_bot - gy_top
    gx_left = @content_x
    gx_right = @content_x + @content_w - 1
    gw = gx_right - gx_left

    # Axes
    @gfx.draw_line(gx_left, gy_bot, gx_right, gy_bot, COLOR_AXIS)
    @gfx.draw_line(gx_left, gy_top, gx_left, gy_bot, COLOR_AXIS)

    return if hist.length < 2 || gh <= 0 || gw <= 0

    denom = HIST_LEN - 1
    prev_x = nil
    prev_y = nil
    i = 0
    while i < hist.length
      v = hist[i]
      px = gx_left + (i * gw) / denom
      py = gy_bot - (v * gh) / peak
      py = gy_top if py < gy_top
      py = gy_bot if py > gy_bot
      if prev_x
        @gfx.draw_line(prev_x, prev_y, px, py, color)
      end
      prev_x = px
      prev_y = py
      i += 1
    end
  end

  # Page 3 furniture. The rows themselves are refresh_tasks' business.
  def draw_tasks_heading
    @gfx.draw_text(@content_x, @user_area_y0 + 2, "Tasks", COLOR_TEXT, COLOR_BG)
    nil
  end

  # Page 3: who is running, and [X] to ask a user task to end.
  #
  # Called once a second, and the answer is almost always "nothing moved". So
  # the list is boiled down to one string and compared with the last one: if
  # it matches, not a single pixel is drawn and flush does not present. The
  # armed row and the kernel's last message are part of the signature because
  # they change what is on screen too.
  def refresh_tasks
    procs = FmrbApp.ps
    my_pid = FmrbApp.pid
    sig = task_signature(procs, my_pid)
    if sig == @task_sig
      @ui.flush
      return
    end
    @task_sig = sig

    msg_y = @nav_y - CHAR_H - 1
    label_chars = (@content_w - KILL_BTN_W - 4) / CHAR_W
    # One wipe for the whole row block, so rows that disappeared leave nothing.
    @gfx.fill_rect(@content_x, @rows_y0, @content_w, msg_y - @rows_y0, COLOR_BG)

    y = @rows_y0
    row = 0
    svc_row = 0
    y_limit = msg_y - 1
    if procs
      i = 0
      while i < procs.size && y < y_limit
        p = procs[i]
        pid = p[:id]
        name = (p[:name] || "?").to_s
        label = "#{pid} #{name}"
        label = label[0, label_chars] if label.length > label_chars
        @gfx.draw_text(@content_x, y, label,
                       p[:state] == 2 ? COLOR_TEXT : COLOR_DIM, COLOR_BG)

        # The shell and the monitor are user tasks too; the kernel refuses to
        # end the one that asked, so the monitor's own row gets no button.
        if p[:type] == TYPE_USER && pid != my_pid && row < @max_rows
          id = KILL_IDS[row]
          @row_pids[row] = pid
          @ui.set_text(id, @kill_armed_pid == pid ? "[?]" : "[X]")
          @ui.move(id, @kill_btn_x - @user_area_x0, y - @user_area_y0,
                   KILL_BTN_W, ROW_H)
          @ui.set_visible(id, true)
          row += 1
        end
        y += ROW_H
        i += 1

        # The services inside the host, as child rows directly under it.
        next unless name == SVC_HOST_NAME
        j = 0
        while j < @svcs.size && svc_row < SVC_IDS.size && y < y_limit
          svc_row = draw_svc_row(@svcs[j], y, svc_row)
          y += ROW_H
          j += 1
        end
      end
    end

    # Buttons past the end of the list go away; their holes are painted with
    # COLOR_BG because that is the bg FmrbUI was built with.
    while row < @max_rows
      @row_pids[row] = nil
      @ui.set_visible(KILL_IDS[row], false)
      row += 1
    end
    while svc_row < SVC_IDS.size
      @svc_names[svc_row] = nil
      @ui.set_visible(SVC_IDS[svc_row], false)
      svc_row += 1
    end

    if @kill_msg
      text = @kill_msg
      max_chars = @content_w / CHAR_W
      text = text[0, max_chars] if text.length > max_chars
      @gfx.fill_rect(@content_x, msg_y, @content_w, CHAR_H, COLOR_BG)
      @gfx.draw_text(@content_x, msg_y, text, COLOR_TEXT, COLOR_BG)
    end
    # flush presents when it drew a widget; if only the text rows moved it
    # returns false and the present is ours to make.
    @gfx.present unless @ui.flush
    nil
  end

  # One service, indented under the host. The text is built to fit the column
  # the button leaves (see SVC_BTN_W): the error count is only shown when
  # there is one, because the common row is already at the limit.
  def draw_svc_row(svc, y, svc_row)
    name = svc["name"].to_s
    state = svc["state"].to_s
    errors = svc["errors"].to_i
    text = "+#{name} #{state}"
    text = "#{text} e#{errors}" if errors > 0
    chars = (@content_w - SVC_BTN_W - 4) / CHAR_W
    text = text[0, chars] if text.length > chars
    @gfx.draw_text(@content_x, y, text,
                   state == "running" ? COLOR_TEXT : COLOR_DIM, COLOR_BG)
    id = SVC_IDS[svc_row]
    @svc_names[svc_row] = name
    # A disabled service is not started from here: switching it back on is a
    # decision that outlives this boot, and that lives in the shell.
    if state == "disabled"
      @ui.set_visible(id, false)
    else
      @ui.set_text(id, state == "running" ? "[stop]" : "[start]")
      @ui.move(id, @content_x + @content_w - SVC_BTN_W - @user_area_x0,
               y - @user_area_y0, SVC_BTN_W, ROW_H)
      @ui.set_visible(id, true)
    end
    svc_row + 1
  end

  # What the page would look like, as one string. Rebuilt every second and
  # thrown away; the rows themselves cost more.
  def task_signature(procs, my_pid)
    sig = +"#{@kill_armed_pid}|#{@kill_msg}|"
    return sig if procs.nil?
    i = 0
    while i < procs.size
      p = procs[i]
      sig << "#{p[:id]},#{p[:state]},#{p[:type]},#{p[:name]};"
      i += 1
    end
    sig << my_pid.to_s
    # The service rows are part of the picture, so a state or error count that
    # moved has to repaint the page like anything else.
    i = 0
    while i < @svcs.size
      row = @svcs[i]
      sig << "|#{row["name"]},#{row["state"]},#{row["errors"]}"
      i += 1
    end
    sig
  end

  def draw_nav
    # "< 1/2 >" centered on the bottom nav strip.
    y = @nav_y + 1
    @gfx.draw_text(@content_x, y, "<", COLOR_NAV_HI, COLOR_BG)
    right_x = @content_x + @content_w - CHAR_W
    @gfx.draw_text(right_x, y, ">", COLOR_NAV_HI, COLOR_BG)

    page_label = "#{@page + 1}/#{NUM_PAGES}"
    label_x = @content_x + (@content_w - page_label.length * CHAR_W) / 2
    @gfx.draw_text(label_x, y, page_label, COLOR_TEXT, COLOR_BG)
  end

  def array_max(arr)
    return 0 if arr.length == 0
    m = arr[0]
    i = 1
    while i < arr.length
      m = arr[i] if arr[i] > m
      i += 1
    end
    m
  end
end

Log.info("MonitorApp.new")
begin
  app = MonitorApp.new
  Log.info("MonitorApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")

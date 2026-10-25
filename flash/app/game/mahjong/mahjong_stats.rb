# Mahjong table: the players' stats (MJ7, doc/ruby_asterism/report/mj7.md).
# Part of MahjongApp, opened from the recent games (mahjong_replay.rb, S or
# the button) and read only then (st_open requires this file).
#
# The game host counts every game kept in its records (asterism-games
# README "Stats"): stats(days, game, scope, from) gives a row per person
# (by the name at the table) and per kind (CPU, Claude, Jev): games, ranks,
# rounds, wins, deal-ins, riichi, the average score and the rank points.
# The screen asks for every page, then draws one table: the averages and
# rates are worked out here from the counts (integers only).
#
# Keys: up / down scroll, P the period (all, 30 days, 7 days), T the scope
# (every table and room, or this table: in a room the room's), Esc back to
# the recent games.

class MahjongApp < FmrbApp
  B_ST_PERIOD = 122
  B_ST_SCOPE = 123
  B_ST_BACK = 125
  ST_DAYS = [0, 30, 7]
  ST_MAX_ROWS = 120

  alias st_base_rp_draw rp_draw
  alias st_base_rp_key rp_key
  alias st_base_rp_poll rp_poll
  alias st_base_run_button run_button
  alias st_base_status_line status_line

  def st_start
    st_strings
    @st = true
    @st_days = 0
    @st_scope = 0
    @st_top = 0
    @st_rows = []
    @st_games = 0
    @st_f = nil
    @st_note = t(:mj_rp_loading)
    @dirty = true
    st_call(0)
    Log.info("mahjong: stats of #{@rp}")
  end

  def st_close
    @st = false
    @st_rows = nil
    @st_f = nil
    @dirty = true
  end

  def st_reload
    @st_rows = []
    @st_games = 0
    @st_top = 0
    @st_f = nil
    @st_note = t(:mj_rp_loading)
    @dirty = true
    st_call(0)
  end

  def st_call(from)
    return if @st_f
    px = ::Asterism[@rp, timeout: CALL_TIMEOUT]
    @st_f = px.async.stats(ST_DAYS[@st_days], "", @st_scope == 0 ? "all" : "table", from)
  rescue ::Asterism::Error => e
    @st_f = nil
    @st_note = e.message.to_s
    @dirty = true
  end

  def rp_poll
    return st_base_rp_poll unless @st
    # The game starts while this player sits at the table: back to it.
    if @tc.token && @seat && @view && @view["st"] != "wait"
      st_close
      return st_base_rp_poll
    end
    f = @st_f
    return unless f && f.done?
    @st_f = nil
    begin
      r = f.value
      if r.is_a?(Hash) && r["rows"].is_a?(Array)
        @st_rows += r["rows"]
        @st_games = r["games"].to_i
        if r["next"].is_a?(Integer) && @st_rows.size < ST_MAX_ROWS
          st_call(r["next"])
        else
          @st_note = @st_rows.empty? ? t(:mj_rp_none) : ""
        end
      else
        @st_note = r.is_a?(Hash) && r["error"] ? r["error"].to_s : t(:mj_st_old_host)
      end
    rescue ::Asterism::Error => e
      @st_note = "#{t(:mj_host_away)} (#{e.class})"
    end
    @dirty = true
  end

  def rp_key(k)
    return st_base_rp_key(k) unless @st
    sc = k[0]
    ch = k[1]
    if sc == FmrbConst::KEY_ESC || ch == 27
      st_close
    elsif sc == FmrbConst::KEY_UP
      @st_top -= 1 if @st_top > 0
    elsif sc == FmrbConst::KEY_DOWN
      @st_top += 1 if @st_top + 1 < @st_rows.size
    elsif ch == 112 || ch == 80
      st_period
    elsif ch == 116 || ch == 84
      st_scope
    end
    @dirty = true
  end

  def st_period
    @st_days = (@st_days + 1) % ST_DAYS.size
    st_reload
  end

  def st_scope
    @st_scope = 1 - @st_scope
    st_reload
  end

  def run_button(type, tiles)
    return st_base_run_button(type, tiles) unless @rp && @st
    if type == B_ST_PERIOD
      st_period
    elsif type == B_ST_SCOPE
      st_scope
    elsif type == B_ST_BACK || type == B_LEAVE
      st_close
    end
    @dirty = true
  end

  def status_line
    @st ? t(:mj_st_keys) : st_base_status_line
  end

  # ------------------------------------------------------------ drawing

  def rp_draw
    return st_base_rp_draw unless @st
    t0 = Machine.board_millis
    @xl_items = nil
    @hits.clear
    clear_user_area(C_FELT)
    fnt(12, @k)
    st_draw
    @gfx.present
    perf_note(:draw, Machine.board_millis - t0)
  end

  # The columns that fit: [label, width] after the name (the 426x240
  # table drops the last three).
  def st_columns
    cols = [[t(:mj_st_games), 40], [t(:mj_st_rank), 56], [t(:mj_st_top), 52], [t(:mj_st_win), 52],
            [t(:mj_st_dealin), 52], [t(:mj_st_riichi), 56], [t(:mj_st_score), 60], [t(:mj_st_points), 60]]
    cols = cols[0, 5] unless @big
    cols
  end

  def st_draw
    x = @x0 + u(8)
    y = @y0 + u(8)
    parts = @rp.to_s.split("/")
    bl = t(:mj_rp_back)
    bw = tw(bl) + u(12)
    bx = @x0 + @w - bw - u(8)
    button(B_ST_BACK, [], bx, y - u(3), bl)
    sl = @st_scope == 0 ? t(:mj_st_all) : t(:mj_st_table)
    sw = tw(sl) + u(12)
    bx -= sw + u(6)
    button(B_ST_SCOPE, [], bx, y - u(3), sl)
    pl = [t(:mj_st_days0), t(:mj_st_days30), t(:mj_st_days7)][@st_days]
    pw = tw(pl) + u(12)
    bx -= pw + u(6)
    button(B_ST_PERIOD, [], bx, y - u(3), pl)
    text(x, y, fit("#{t(:mj_st_title)}  #{parts[0]}/#{parts[2]}", bx - x - u(6)))
    y += u(20)
    text(x, y, fit("#{@st_games}#{t(:mj_st_games_n)}  #{t(:mj_st_hint)}", @w - u(16)), C_DIM)
    y += u(20)
    unless @st_note.to_s.empty?
      text(x, y, fit(@st_note.to_s, @w - u(16)), C_TURN)
      y += u(18)
    end
    cols = st_columns
    fixed = 0
    i = 0
    while i < cols.size
      fixed += u(cols[i][1])
      i += 1
    end
    nw = @w - u(16) - fixed
    # The header.
    text(x, y, t(:mj_st_name), C_DIM)
    cx = x + nw
    i = 0
    while i < cols.size
      cw = u(cols[i][1])
      text(cx + cw - tw(cols[i][0]) - u(4), y, cols[i][0], C_DIM)
      cx += cw
      i += 1
    end
    y += u(16)
    @gfx.fill_rect(x, y - u(2), @w - u(16), 1, C_DIM)
    rows = (@y0 + @h - y - u(4)) / u(17)
    rows = 1 if rows < 1
    @st_top = 0 if @st_top >= @st_rows.size
    i = @st_top
    while i < @st_rows.size && i < @st_top + rows
      st_draw_row(@st_rows[i], x, y, nw, cols)
      y += u(17)
      i += 1
    end
  end

  def st_draw_row(r, x, y, nw, cols)
    human = r["kind"].to_s == "human"
    name = r["name"].to_s
    color = !human ? C_DIM : (name == @name ? C_TURN : C_TEXT)
    text(x, y, fit(name, nw - u(6)), color)
    vals = st_values(r)
    cx = x + nw
    i = 0
    while i < cols.size
      cw = u(cols[i][1])
      v = vals[i]
      text(cx + cw - tw(v) - u(4), y, v, color)
      cx += cw
      i += 1
    end
  end

  # The row's numbers as text, in st_columns' order.
  def st_values(r)
    g = r["games"].to_i
    rk = r["ranks"].is_a?(Array) ? r["ranks"] : []
    sum = 0
    i = 0
    while i < rk.size
      sum += rk[i].to_i * (i + 1)
      i += 1
    end
    rounds = r["rounds"].to_i
    pt = r["points"]
    pq = pt.is_a?(Float) ? (pt * 10).round : pt.to_i * 10
    [g.to_s, st_fixed(g > 0 ? (sum * 100 + g / 2) / g : 0, 100), st_pct(rk[0].to_i, g), st_pct(r["wins"].to_i, rounds),
     st_pct(r["dealins"].to_i, rounds), st_pct(r["riichi"].to_i, rounds), r["avg_score"].to_i.to_s, st_fixed(pq, 10)]
  end

  # n / d as "23.1%" ("-" without d).
  def st_pct(n, d)
    return "-" if d <= 0
    st_fixed((n * 1000 + d / 2) / d, 10) + "%"
  end

  # An integer of 1/scale as a decimal ("2.41", "-5.5").
  def st_fixed(q, scale)
    sign = q < 0 ? "-" : ""
    a = q < 0 ? -q : q
    f = (a % scale).to_s
    f = "0#{f}" if scale == 100 && a % scale < 10
    "#{sign}#{a / scale}.#{f}"
  end

  def st_strings
    @str[:mj_st_title] = "成績"
    @str[:mj_st_old_host] = "このホストは成績を出さない (0.14.0 より前)"
    @str[:mj_st_all] = "全体[T]"
    @str[:mj_st_table] = "この卓[T]"
    @str[:mj_st_days0] = "全期間[P]"
    @str[:mj_st_days30] = "30日[P]"
    @str[:mj_st_days7] = "7日[P]"
    @str[:mj_st_games_n] = " 対局"
    @str[:mj_st_hint] = "人は表示名ごと、CPU・AI は種類ごと"
    @str[:mj_st_name] = "名前"
    @str[:mj_st_games] = "対局"
    @str[:mj_st_rank] = "平均順位"
    @str[:mj_st_top] = "1位率"
    @str[:mj_st_win] = "和了率"
    @str[:mj_st_dealin] = "放銃率"
    @str[:mj_st_riichi] = "リーチ率"
    @str[:mj_st_score] = "平均点"
    @str[:mj_st_points] = "順位点"
    @str[:mj_st_keys] = "成績 (↑↓ P T Esc)"
  end
end

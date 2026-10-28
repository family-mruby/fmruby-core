# The small notices at the top right (doc/linux_native, L14), for
# SystemDesktopApp.
#
# The PC only: FmrbApp.notices_gen is -1 on every device, the simulation and
# the browser, and then nothing here runs again after the first update (no
# call per update, nothing drawn). On the PC the notices come from the C side's store
# (native/sys/linux_notify.h): Linux programs' freedesktop notifications,
# FmrbApp.notify from the apps, the volume keys' level, and the session's
# "started again" after an abnormal end. The store decides when each ends;
# this only draws what it holds, newest at the top, when its generation
# moves (looked at on every update, 0.5 s). No history, no buttons: a notice
# is shown for its few seconds and is gone.
#
# Drawn on the foreground canvas over everything (as the menus are), each in
# a composite region of its own. A click goes to whatever is under it.

module NoticesMixin
  NTC_W = 184
  NTC_GAP = 4
  NTC_LINE = 10
  NTC_PAD = 3
  NTC_BG = FmrbConst::THEME_WINDOW_BG
  NTC_TEXT = FmrbConst::THEME_TEXT
  NTC_HEAD_BG = FmrbConst::THEME_MENU_BG
  NTC_HEAD_TEXT = FmrbConst::THEME_TEXT_LIGHT
  NTC_BORDER = FmrbConst::THEME_BORDER
  NTC_ACCENT = FmrbConst::THEME_HIGHLIGHT

  # Whether this machine shows notices is asked on the first update after
  # the boot (tick_notices), not here: the store starts a notice's time when
  # it is first asked (the session's "started again" waits for the desktop).
  def setup_notices
    @ntc_on = nil
    @ntc_gen = -2
    @ntc_items = []
  end

  # From on_update. true when the notices changed (they are redrawn).
  def tick_notices
    if @ntc_on.nil?
      @ntc_gen = FmrbApp.notices_gen
      @ntc_on = @ntc_gen >= 0
      return false unless @ntc_on
      refresh_notices
      return true
    end
    return false unless @ntc_on
    g = FmrbApp.notices_gen
    return false if g == @ntc_gen
    @ntc_gen = g
    refresh_notices
    true
  end

  def refresh_notices
    before = @ntc_items.size
    @ntc_items = []
    text = FmrbApp.notices
    lines = text.split("\n")
    i = 0
    while i < lines.size
      item = notice_item(lines[i])
      @ntc_items << item if item
      i += 1
    end
    Log.info("Desktop: #{@ntc_items.size} notice(s) on screen") if before > 0 || @ntc_items.size > 0
    update_composite_regions
    draw_foreground
  end

  # One line of the store: [kind, header, title, body lines, level].
  def notice_item(line)
    f = line.split("\t")
    return nil if f.size < 2
    kind = f[1]
    app = f[2].to_s
    summary = f[3].to_s
    body = f[4].to_s
    w = NTC_W - 2 * NTC_PAD - 2
    if kind == "volume"
      pct = summary.to_i
      key = body == "muted" ? :notice_muted : :notice_volume
      return [kind, nil, sprintf(FmrbI18n.t(key), pct), [], pct]
    end
    if kind == "restarted"
      return [kind, "Family mruby", FmrbI18n.t(:notice_restarted),
              notice_wrap(sprintf(FmrbI18n.t(:notice_restarted_why), body), w, 2), nil]
    end
    head = app.empty? ? "Linux" : app
    title = summary
    lines = []
    parts = body.split("\x1f")
    k = 0
    while k < parts.size && lines.size < 2
      wrapped = notice_wrap(parts[k], w, 2 - lines.size)
      j = 0
      while j < wrapped.size
        lines << wrapped[j]
        j += 1
      end
      k += 1
    end
    if title.empty? && !lines.empty?
      title = lines.shift
    end
    [kind, FmrbI18n.truncate_to(head, w), FmrbI18n.truncate_to(title, w), lines, nil]
  end

  # Up to max lines of at most px each; the last is cut with "..".
  def notice_wrap(str, px, max)
    out = []
    rest = str.to_s
    while !rest.empty? && out.size < max
      if FmrbI18n.text_width(rest) <= px || out.size == max - 1
        out << FmrbI18n.truncate_to(rest, px)
        break
      end
      n = notice_fit_chars(rest, px)
      n = 1 if n < 1
      out << rest[0, n]
      rest = rest[n, rest.length - n].to_s
    end
    out
  end

  # How many characters of str fit in px.
  def notice_fit_chars(str, px)
    n = 0
    w = 0
    len = str.length
    while n < len
      cw = FmrbI18n.text_width(str[n])
      break if w + cw > px
      w += cw
      n += 1
    end
    n
  end

  def notice_height(item)
    rows = 1
    rows += 1 if item[1]
    rows += item[3].size
    rows += 1 if item[4]
    2 * NTC_PAD + rows * NTC_LINE + 1
  end

  def notice_x
    @window_width - NTC_W - 4
  end

  # The rectangles, top to bottom (for the regions and the drawing).
  def notice_rects
    rects = []
    y = self.class::MENU_BAR_HEIGHT + 4
    i = 0
    while i < @ntc_items.size
      h = notice_height(@ntc_items[i])
      break if y + h > @window_height
      rects << [notice_x, y, NTC_W, h]
      y += h + NTC_GAP
      i += 1
    end
    rects
  end

  def notice_regions(regions)
    return unless @ntc_on
    rects = notice_rects
    i = 0
    while i < rects.size
      r = rects[i]
      regions << { dst_x: r[0], dst_y: r[1], w: r[2], h: r[3], transparent: false }
      i += 1
    end
  end

  def draw_notices
    return unless @ntc_on
    rects = notice_rects
    i = 0
    while i < rects.size
      draw_notice(@ntc_items[i], rects[i])
      i += 1
    end
  end

  def draw_notice(item, r)
    x = r[0]
    y = r[1]
    w = r[2]
    h = r[3]
    @gfx.fill_rect(x, y, w, h, NTC_BG)
    @gfx.draw_rect(x, y, w, h, NTC_BORDER)
    ty = y + NTC_PAD
    if item[1]
      @gfx.fill_rect(x + 1, y + 1, w - 2, NTC_LINE + NTC_PAD - 1, NTC_HEAD_BG)
      @gfx.draw_text(x + NTC_PAD + 1, ty, item[1], NTC_HEAD_TEXT, NTC_HEAD_BG, mixed: true)
      ty += NTC_LINE + 1
    end
    @gfx.draw_text(x + NTC_PAD + 1, ty, item[2], NTC_TEXT, NTC_BG, mixed: true)
    ty += NTC_LINE
    lines = item[3]
    j = 0
    while j < lines.size
      @gfx.draw_text(x + NTC_PAD + 1, ty, lines[j], NTC_TEXT, NTC_BG, mixed: true)
      ty += NTC_LINE
      j += 1
    end
    if item[4]
      # the volume's level as a bar
      bw = w - 2 * NTC_PAD - 2
      @gfx.draw_rect(x + NTC_PAD + 1, ty + 1, bw, NTC_LINE - 3, NTC_BORDER)
      fill = (bw - 2) * item[4] / 100
      @gfx.fill_rect(x + NTC_PAD + 2, ty + 2, fill, NTC_LINE - 5, NTC_ACCENT) if fill > 0
    end
  end
end

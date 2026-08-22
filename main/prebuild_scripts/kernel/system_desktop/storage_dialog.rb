# Storage management dialog for SystemDesktopApp.
#
# A small sub-dialog that exposes a single "Clear /cache" action. The cache
# directory on WROVER (/flash/cache) is populated lazily by user apps via
# FmrbGfx#transfer_file (e.g. flappy.rb caches its sprite bitmaps there), so
# clearing it is a safe recovery action — affected apps will re-transfer on
# the next run.
#
# The dialog has two phases: a Clear/Close screen, and a confirm overlay
# rendered inline (the shared ConfirmDialogMixin is wired to kernel messages,
# which we don't need here since FmrbApp._clear_cache is local).

module StorageDialogMixin
  STR_W       = 220
  STR_H       = 100
  STR_TITLE_H = 14
  STR_BG      = FmrbConst::THEME_WINDOW_BG
  STR_TEXT    = FmrbConst::THEME_TEXT
  STR_HEADER  = FmrbConst::THEME_MENU_BG
  STR_BTN     = FmrbConst::THEME_BUTTON
  STR_BTN_H   = 16
  STR_BTN_WARN= 0xE0
  STR_BORDER  = FmrbConst::THEME_BORDER

  CACHE_ROOT = "/cache"

  # ---- Lifecycle ----

  def open_storage_dialog
    @str_open = true
    @str_phase = :idle          # :idle, :confirm, :running, :done
    @str_status = nil
    @str_x = (@window_width - STR_W) / 2
    @str_y = (@window_height - STR_H) / 2
    menu_h = self.class::MENU_BAR_HEIGHT
    @str_y = menu_h + 2 if @str_y < menu_h + 2
    close_launcher
    close_dropdown
    notify_overlay_state(true, @str_x, @str_y, STR_W, STR_H)
    update_composite_regions
    draw_foreground
  end

  def close_storage_dialog
    return unless @str_open
    @str_open = false
    @str_phase = :idle
    hide_storage_widgets
    @ui.flush
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def hit_storage_dialog?(x, y)
    @str_open &&
      x >= @str_x && x < @str_x + STR_W &&
      y >= @str_y && y < @str_y + STR_H
  end

  # ---- Drawing ----

  def draw_storage_dialog
    return unless @str_open
    x = @str_x
    y = @str_y
    @gfx.fill_rect(x, y, STR_W, STR_H, STR_BG)
    @gfx.draw_rect(x, y, STR_W, STR_H, STR_BORDER)
    @gfx.fill_rect(x + 1, y + 1, STR_W - 2, STR_TITLE_H - 1, STR_HEADER)
    @gfx.draw_text(x + 4, y + 3, FmrbI18n.t(:storage), FmrbGfx::WHITE, STR_HEADER, mixed: true)

    # Body line: clarify the target lives on WROVER (/flash/cache there),
    # not the Core's local filesystem, then show any status from the last
    # action below it.
    body_y = y + STR_TITLE_H + 6
    @gfx.draw_text(x + 8, body_y, FmrbI18n.t(:clear_cache_target),
                   STR_TEXT, STR_BG, mixed: true)
    if @str_status
      @gfx.draw_text(x + 8, body_y + 14, @str_status, STR_TEXT, STR_BG, mixed: true)
    end

    if @str_phase == :confirm
      draw_storage_confirm_overlay
    else
      draw_storage_buttons
    end
  end

  def draw_storage_buttons
    btn_y = @str_y + STR_H - 22
    btn_h = 16

    label_close = FmrbI18n.t(:close)
    bw_close = FmrbI18n.text_width(label_close) + 12

    # After a clear has run, the cache is already empty: drop the Clear
    # button so the user doesn't think a second click does something
    # different. Re-opening the dialog from the menu starts fresh in :idle.
    clear_w = @ui.find(:str_clear).w
    if @str_phase == :done
      # After a clear has run the cache is already empty, so the Clear button
      # goes away rather than inviting a second, different-looking click.
      bx = @str_x + (STR_W - bw_close) / 2
      @ui.set_visible(:str_clear, false)
    else
      total = clear_w + 8 + bw_close
      bx = @str_x + (STR_W - total) / 2
      @ui.move(:str_clear, bx, btn_y, clear_w, STR_BTN_H)
      @ui.set_visible(:str_clear, true)
      bx += clear_w + 8
    end
    @ui.move(:str_close, bx, btn_y, bw_close, STR_BTN_H)
    @ui.set_visible(:str_close, true)
    @ui.set_visible(:str_yes, false)
    @ui.set_visible(:str_no, false)
    @ui.invalidate_all
  end

  def draw_storage_confirm_overlay
    # Dim the body area, then render the confirm prompt + Yes/No row.
    bx = @str_x + 4
    by = @str_y + STR_TITLE_H + 4
    bw = STR_W - 8
    bh = STR_H - STR_TITLE_H - 8
    @gfx.fill_rect(bx, by, bw, bh, STR_BG)
    # The localized confirm prompt is pre-wrapped with "\n" so it fits the
    # 220 px dialog width without truncation. Render each line in order.
    msg = FmrbI18n.t(:clear_cache_confirm)
    line_y = by + 4
    msg.split("\n").each do |line|
      line = FmrbI18n.truncate_to(line, STR_W - 16)
      @gfx.draw_text(@str_x + 8, line_y, line, STR_TEXT, STR_BG, mixed: true)
      line_y += 12
    end

    btn_y = @str_y + STR_H - 22
    yes_w = @ui.find(:str_yes).w
    no_w = @ui.find(:str_no).w
    total = yes_w + 8 + no_w
    bxx = @str_x + (STR_W - total) / 2
    @ui.move(:str_yes, bxx, btn_y, yes_w, STR_BTN_H)
    @ui.move(:str_no, bxx + yes_w + 8, btn_y, no_w, STR_BTN_H)
    @ui.set_visible(:str_yes, true)
    @ui.set_visible(:str_no, true)
    @ui.set_visible(:str_clear, false)
    @ui.set_visible(:str_close, false)
    @ui.invalidate_all
  end

  # ---- Interaction ----

  # Clear and Yes are warning-coloured: they throw away the cache and there
  # is no undo. The colour is the only thing separating them from Close and
  # No at a glance, which is what accent: exists for.
  def build_storage_widgets
    lclear = FmrbI18n.t(:clear_cache)
    lclose = FmrbI18n.t(:close)
    @ui.button(:str_clear, 0, 0, FmrbI18n.text_width(lclear) + 12, STR_BTN_H,
               lclear, accent: STR_BTN_WARN)
    @ui.button(:str_close, 0, 0, FmrbI18n.text_width(lclose) + 12, STR_BTN_H,
               lclose)
    @ui.button(:str_yes, 0, 0, FmrbI18n.text_width("Yes") + 16, STR_BTN_H,
               "Yes", accent: STR_BTN_WARN)
    @ui.button(:str_no, 0, 0, FmrbI18n.text_width("No") + 16, STR_BTN_H, "No")
    hide_storage_widgets
    nil
  end

  def hide_storage_widgets
    @ui.set_visible(:str_clear, false)
    @ui.set_visible(:str_close, false)
    @ui.set_visible(:str_yes, false)
    @ui.set_visible(:str_no, false)
    nil
  end

  def handle_storage_dialog_widget(id)
    case id
    when :str_yes then str_run_clear
    when :str_no
      @str_phase = :idle
      draw_foreground
    when :str_clear
      @str_phase = :confirm
      draw_foreground
    when :str_close then close_storage_dialog
    end
    nil
  end

  def str_run_clear
    # Show a running message before the (blocking) RPC. The dialog will not
    # repaint while FmrbApp._clear_cache is waiting on its semaphore, but the
    # text is up before the call so the user has visual feedback that the
    # action was accepted.
    @str_phase = :running
    @str_status = FmrbI18n.t(:clear_cache_running)
    draw_foreground

    begin
      res = FmrbApp._clear_cache(CACHE_ROOT)
    rescue => e
      Log.error("Storage: clear_cache raised: #{e.message}")
      res = nil
    end

    if res && res[:ok]
      deleted = res[:deleted] || 0
      @str_status = sprintf(FmrbI18n.t(:clear_cache_done), deleted)
      Log.info("Storage: cleared #{CACHE_ROOT} (#{deleted} entries)")
    else
      @str_status = FmrbI18n.t(:clear_cache_failed)
      Log.warn("Storage: clear_cache failed: #{res.inspect}")
    end
    @str_phase = :done
    draw_foreground
  end
end

# frozen_string_literal: true
#
# Helpers for the screen scenes (rake regress:screens, doc/regression/
# usage.md chapter 9): what every scene does first, where the desktop's
# menu entries are, opening the editor and the apps.

Regress::Screens.helpers do
  # The desktop's dropdown (system_desktop.app.rb DROPDOWN_ITEMS): the
  # entries in order. The browser build has no Set Clock and no Storage.
  def menu_items
    items = %i[launcher editor file_manager log_viewer monitor]
    items << :set_clock unless web?
    items + %i[config audio_mute]
  end

  def menu_bar_h = 13
  def menu_item_h = 12

  # Every scene, before its block: the menu bar's clock (the date and the
  # seconds, at window_width - 90) is left out of every comparison.
  def before_scene
    mask(w - 90, 0, 90, menu_bar_h)
    # The pointer, parked (below): the page does not always draw it after
    # a reload, and it is not what any scene looks at.
    mask(w - 6, 60, 6, 16)
  end

  # Once per boot: the pointer to a place no scene uses, the desktop at rest
  # remembered (each scene has to give it back).
  def after_boot
    capture
    # The browser build drops a pointer move that comes while it is still
    # busy after the boot: the second one draws the pointer for sure.
    2.times do
      park
      wait_stable(timeout: 20)
    end
    @last
  end

  # The pointer is drawn into the picture, so it goes to the same place
  # before every shot: the sky at the right, under the menu bar.
  def park
    move(w - 6, 60)
  end

  def menu(item)
    idx = menu_items.index(item) or raise Regress::Screens::Failure, "no menu entry #{item}"
    click(30, 5)
    wait_change([0, menu_bar_h, 80, 30])
    click(30, menu_bar_h + idx * menu_item_h + menu_item_h / 2)
  end

  # The title bar colour of a focused window, and a point on the editor's
  # title bar the desktop and its dropdown never colour. The themes differ:
  # Light on the sim (config/system_conf_*.toml), Cyberpunk on the page.
  def title_rgb = web? ? [32, 0, 172] : [218, 36, 85]

  # Until a window's title bar shows at column x, somewhere in the top half
  # (the Config dialog is centred, so its title sits lower when it has
  # fewer rows, as on the page).
  def wait_title(what, x: w / 2, timeout: 10)
    wait_until(what, timeout: timeout) do |img|
      (menu_bar_h...(img.h / 2)).any? { |y| img.pixel(x, y) == title_rgb }
    end
  end

  def editor_probe = [150, 20]

  def open_editor
    menu :editor
    wait_until("the editor's window", timeout: 15) { |img| img.pixel(*editor_probe) == title_rgb }
    park
    wait_stable
  end

  # An app by its path; the browser build has no door for that, so there it
  # is the launcher's index (arrow keys from the first icon, then Enter).
  def open_app(path, web_index: nil)
    if web?
      raise Regress::Screens::Failure, "open_app(#{path}): no launcher index for the browser build" unless web_index
      menu :launcher
      wait_title("the launcher")
      web_index.times { key "right" } # the launcher opens on the first icon
      key "enter"
    else
      launch path
    end
    pause 500
    park
  end

  # The dialogs' buttons (Config's Cancel and Save).
  def button_rgb = web? ? [74, 36, 172] : [109, 0, 0]

  # Click the first button of a colour in a rectangle, searching from the
  # bottom (a dialog's footer) and taking the leftmost one there: where a
  # dialog's buttons sit depends on the screen and on the rows it shows. A
  # button is a run of 10 to 120 pixels (the frame of the dialog, drawn in
  # the same colour, is longer).
  def click_button(rgb, rect)
    img = capture
    x0, y0, rw, rh = rect
    (y0 + rh - 1).downto(y0) do |y|
      runs = []
      x = x0
      while x < x0 + rw
        if img.pixel(x, y) == rgb
          start = x
          x += 1 while x < x0 + rw && img.pixel(x, y) == rgb
          runs << [start, x - 1]
        else
          x += 1
        end
      end
      left, right = runs.find { |a, b| (b - a + 1).between?(10, 120) }
      next unless left
      cx = (left + right) / 2
      top = y
      top -= 1 while top > y0 && img.pixel(cx, top - 1) == rgb
      return click(cx, (top + y) / 2)
    end
    raise Regress::Screens::Failure, "no button of #{rgb.inspect} in #{rect.inspect}"
  end
end

# frozen_string_literal: true
#
# Helpers for the screen scenes (rake regress:screens, doc/regression/
# usage.md chapter 9): what every scene does first, where the desktop's
# menu entries are, opening the editor and the apps, and the regions the
# scenes watch. A region is where something has to change, never a picture
# to match: nothing here depends on the theme, the menu bar's look or the
# icons on the desktop.

Regress::Screens.helpers do
  # The desktop's dropdown (system_desktop.app.rb DROPDOWN_ITEMS): the
  # first entries, the same on every configuration.
  def menu_items = %i[launcher editor file_manager]

  def menu_bar_h = 13
  def menu_item_h = 12

  # The native program's standard desktop (machine sdl) has a power entry,
  # so its menu bar draws the power button right of the clock and the
  # status cells sit 14 px further left. The keys desktop (native-keys) has
  # its own bar with the clock at the right end.
  def power_button_w = (native? && name == "native") ? 14 : 0

  # Every scene, before its block: the menu bar's clock (the date and the
  # seconds, at window_width - 90, left of the power button if there is
  # one) is left out of every wait and comparison.
  def before_scene
    mask(w - 90 - power_button_w, 0, 90, menu_bar_h)
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
  # before the scene waits on a region: the right edge, under the menu bar.
  def park
    move(w - 6, 60)
  end

  def menu(item)
    idx = menu_items.index(item) or raise Regress::Screens::Failure, "no menu entry #{item}"
    click(6, 5)
    wait_change([0, menu_bar_h, 60, 30], what: "the desktop's menu")
    click(30, menu_bar_h + idx * menu_item_h + menu_item_h / 2)
  end

  # Where a new Window shows on the standard desktop (the top left, a few
  # pixels lower for each one already open), inside its body: the region a
  # Window coming up has to change. Clear of the menu bar, the taskbar and
  # the Window's title bar.
  def window_body_rect = [24, 48, 100, 60]

  # The screen between the menu bar and the taskbar's row: where the
  # desktop's own Windows show (a same-run comparison, never a baseline).
  def below_menu_bar_rect = [0, menu_bar_h, w, h - menu_bar_h - 16]

  # The editor's first text line, where a typed key shows (the editor's
  # Window sits at the top left; the native program's keys desktop shows it
  # full screen under its bar).
  def editor_text_rect
    name == "native-keys" ? [0, 12, 40, 24] : [4, 30, 40, 22]
  end

  # The editor from the desktop's menu: its Window changes the top left of
  # the screen, and where there is a process list, the editor is in it.
  def open_editor
    menu :editor
    wait_differs(desktop_at_rest, window_body_rect, timeout: 15, what: "the editor's Window")
    wait_app_named(/Editor/, "the editor") if process_list?
    park
    wait_stable
  end

  # Until an app whose name matches is running; its pid.
  def wait_app_named(re, what, timeout: 10)
    pid = nil
    wait_for_apps("#{what} in the process list", timeout: timeout) do |list|
      pid = list.find { |_p, n| n =~ re }&.first
    end
    pid
  end

  def wait_for_apps(what, timeout: 10)
    deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + timeout
    loop do
      return confirm(what) if yield(apps)
      raise Regress::Screens::Failure, "timed out after #{timeout}s waiting for #{what} (running: #{apps.values.join(', ')})" if Process.clock_gettime(Process::CLOCK_MONOTONIC) > deadline
      sleep 0.25
    end
  end

  # An app by its path (every configuration has a door for it: the debug
  # server's spawn, or the page's launch slot on the browser build), as
  # rake regress:live opens its apps. Its pid.
  def open_app(path)
    pid = launch path
    pause 500
    park
    pid
  end

  # An app by its path, checked: it opens (the path is taken), its Window
  # changes the region where a new Window shows, and where there is a
  # process list it is in it.
  def open_app_checked(path)
    mark
    pid = launch path
    wait_change(window_body_rect, timeout: 15, what: "#{path}'s Window")
    wait_running(pid, path)
    pid
  end
end

# frozen_string_literal: true
#
# A few apps opened by their paths, one after another so that they run side
# by side: each one's Window changes the screen where a new Window shows
# and, where there is a process list (the sim, the native program), it is
# in it; then each is closed and has to leave the list. The browser build
# has no process list: there the launch's answer (a pid, or the refusal of
# the path) and the Window say it opened, and the runner's reload closes
# them. After the scene the desktop has to be back as it was after the
# boot (the runner).
#
# One app of each kind that comes up on every configuration with no
# network, sensor or input: mruby (kamon, the picoruby demo) and Lua.

Regress::Screens.scene "apps", on: %w[modern retro web native modern-mruby retro-mruby] do |s|
  paths = %w[/app/demo/kamon.app.rb /app/demo/picoruby.app.rb /app/lua/lua.app.lua]
  pids = paths.map { |path| [path, s.open_app_checked(path)] }
  # Still there once all are open (none quit or died on the way).
  s.pause 1000
  pids.each { |path, pid| s.wait_running(pid, path) }
  s.wait_differs(s.desktop_at_rest, s.window_body_rect, what: "the apps' Windows still up")
  pids.reverse_each { |path, pid| s.close_app(pid, path) } if s.process_list?
end

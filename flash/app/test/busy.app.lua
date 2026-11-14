-- Busy-loop test app for the kill / starvation checks (doc/app_kill_fix/).
-- Draws a label, then spins forever inside on_create without returning.
-- Expected: the green LED switches to the 0.5s saturated pattern, input
-- stays responsive, the clock keeps ticking, and both the close button
-- and "kill pid=N" via debugd (or the web console debug panel) end it
-- (the shell unwinds a call that has run for a second once it is asked to
-- end, fmrb_app_shell.h).
-- No .app.toml, so the launcher does not list it; start it with
-- "run /app/test/busy.app.lua" or spawn from the debug panel.
function on_create()
    gfx:draw_text("busy loop", 10, 10, FmrbApp.THEME_TEXT)
    gfx:present()
    print("busy: entering loop")
    while true do end
end

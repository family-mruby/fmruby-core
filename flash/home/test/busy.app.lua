-- Busy-loop test app for the kill / starvation checks (doc/app_kill_fix/).
-- Spawns a window, draws a label, then spins forever without yielding.
-- Expected: the green LED switches to the 0.5s saturated pattern, input
-- stays responsive, the clock keeps ticking, and both the close button
-- and "kill pid=N" via debugd (or the web console debug panel) end it.
-- Lives under /home/test so the launcher does not list it; start it with
-- "run /home/test/busy.app.lua" or spawn from the debug panel.
local canvas_id = FmrbApp.create_canvas(FmrbApp.WINDOW_WIDTH, FmrbApp.WINDOW_HEIGHT)
local gfx = FmrbGfx.new(canvas_id)
gfx:clear(FmrbGfx.WHITE)
gfx:draw_text("busy loop", 10, 10, 0x03)
gfx:present()
print("busy: entering loop")
while true do end

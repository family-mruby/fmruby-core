-- Sample Lua app for Family mruby.
--
-- The system makes the window, draws its frame and runs the loop; the app
-- only defines functions. gfx draws from the top-left of the area inside the
-- frame, the same as @gfx of a Ruby app.

local seconds = 0
local clicks = 0

local function draw_counter()
    -- Erase in the page colour, not white: on a dark theme a white band
    -- would be left behind.
    gfx:fill_rect(0, 40, FmrbApp.user_area_width, 10, FmrbApp.THEME_WINDOW_BG)
    gfx:draw_text("Running: " .. tostring(seconds) .. "s", 8, 40, FmrbApp.THEME_BORDER)
    gfx:fill_rect(0, 52, FmrbApp.user_area_width, 10, FmrbApp.THEME_WINDOW_BG)
    gfx:draw_text("Clicks: " .. tostring(clicks), 8, 52, FmrbApp.THEME_BORDER)
    gfx:present()
end

function on_create()
    print("Hello from Lua! " .. _VERSION)
    FmrbApp.clear_user_area()
    -- Theme ink, not FmrbGfx.BLUE: 0x03 vanishes on a dark page.
    gfx:draw_text("Hello from Lua!", 2, 2, FmrbApp.THEME_TEXT)
    gfx:draw_text("FMRuby Graphics", 2, 12, FmrbApp.THEME_TEXT)
    gfx:draw_text(_VERSION, 2, 22, FmrbGfx.RED)
    draw_counter()
end

function on_update()
    draw_counter()
    seconds = seconds + 1
    return 1000
end

function on_event(ev)
    -- A press on the title bar reaches the app too (y < 0, above the
    -- area), as it does a Ruby app; only presses in the area count.
    if ev.type == "mouse_down" and ev.button == 1 and ev.y >= 0 then
        clicks = clicks + 1
        gfx:fill_circle(ev.x, ev.y, 2, FmrbApp.THEME_HIGHLIGHT)
        draw_counter()
    elseif ev.type == "key_down" and ev.character == string.byte("q") then
        FmrbApp.stop()
    end
end

function on_destroy()
    print("Lua app ends after " .. tostring(seconds) .. "s")
end

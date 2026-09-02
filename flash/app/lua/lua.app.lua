FmrbApp.sleep(100)

-- Create canvas for this app
local canvas_id = FmrbApp.create_canvas(FmrbApp.WINDOW_WIDTH, FmrbApp.WINDOW_HEIGHT)
print("Canvas created: ID=" .. tostring(canvas_id))

-- Initialize graphics
local gfx = FmrbGfx.new(canvas_id)
print("Graphics initialized")

-- Sample Lua Application for FMRuby
print("Hello from Lua!")
print("Lua version: " .. _VERSION)
print("Sample Lua app is running successfully")

function draw_window_frame()
    local TITLE_BAR_H = 11
    local CORNER_R = 4
    local w = FmrbApp.WINDOW_WIDTH
    local h = FmrbApp.WINDOW_HEIGHT
    -- Colours come from the system theme, so this window matches every other
    -- one. Spelling them out (this used to say 0xC5 and 0x60) is what left a
    -- Lua window in the classic colours after the theme changed.
    local bar = FmrbApp.THEME_MENU_BG
    local on_bar = FmrbApp.THEME_TEXT_LIGHT
    -- Title bar background: rounded rect on top, flatten the bottom edge
    gfx:fill_round_rect(0, 0, w, TITLE_BAR_H, CORNER_R, bar)
    gfx:fill_rect(0, CORNER_R, w, TITLE_BAR_H - CORNER_R, bar)
    -- Hamburger menu (3 horizontal lines)
    gfx:fill_rect(2, 3, 8, 1, on_bar)
    gfx:fill_rect(2, 5, 8, 1, on_bar)
    gfx:fill_rect(2, 7, 8, 1, on_bar)
    gfx:draw_text("Lua App", 12, 2, on_bar)
    -- Close button
    gfx:fill_circle(w - 6, 5, 3, on_bar)
    -- Rounded window border
    gfx:draw_round_rect(0, 0, w, h, CORNER_R, FmrbApp.THEME_BORDER)
end

-- Clear screen with the theme's page colour
gfx:clear(FmrbApp.THEME_WINDOW_BG)
draw_window_frame()
gfx:present()

-- Print window info from FmrbApp
print("Window size: " .. tostring(FmrbApp.WINDOW_WIDTH) .. "x" .. tostring(FmrbApp.WINDOW_HEIGHT))
print("Headless mode: " .. tostring(FmrbApp.HEADLESS))



-- -- Draw some rectangles
-- -- gfx:fill_rect(10, 10, 100, 50, FmrbGfx.RED)
-- gfx:fill_rect(0, 0, 100, 100, FmrbGfx.RED)
-- gfx:present()
-- FmrbApp.sleep(500)
-- gfx:fill_rect(0, 0, 100, 100, FmrbGfx.BLUE)
-- gfx:present()
-- FmrbApp.sleep(500)
-- gfx:fill_rect(0, 0, 100, 100, FmrbGfx.BLACK)
-- FmrbApp.sleep(30)

-- Main loop to keep app running
print("Entering main loop...")
local running = true
local frame_count = 0

-- Theme ink, not FmrbGfx.BLUE: 0x03 is very dark and vanished into
-- the page once the page followed a dark theme.
gfx:draw_text("Hello from Lua!", 3, 14, FmrbApp.THEME_TEXT)
gfx:draw_text("FMRuby Graphics", 3, 24, FmrbApp.THEME_TEXT)
gfx:draw_text(_VERSION, 3, 34, FmrbGfx.RED)
gfx:draw_text("Running: 0 s", 10, 54, FmrbApp.THEME_BORDER)
gfx:present()

while running do
    -- Simple animation: update frame counter
    frame_count = frame_count + 1

    -- Update display every 60 frames (about 1 second at 60fps)
    if frame_count % 60 == 0 then
        local seconds = math.floor(frame_count / 60)
        -- Clear the counter area and redraw

        -- Erase in the page colour, not white: on a dark theme a white
        -- band would be left behind.
        gfx:fill_rect(0, 54, FmrbApp.WINDOW_WIDTH-1, 64, FmrbApp.THEME_WINDOW_BG)
        gfx:draw_text("Running: " .. tostring(seconds) .. "s", 10, 54, FmrbApp.THEME_BORDER)
        draw_window_frame()
        gfx:present()
        print("Running: " .. tostring(seconds) .. "s")
    end

    -- Sleep for 16ms to maintain approximately 60fps
    FmrbApp.sleep(16)
end

print("Lua app execution completed")

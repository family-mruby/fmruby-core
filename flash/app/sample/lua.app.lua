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
    gfx:fill_rect(0, 0, FmrbApp.WINDOW_WIDTH, 12, 0xC5)
    gfx:fill_rect(2, 2, 8, 8, 0x60)
    gfx:draw_rect(0, 0, FmrbApp.WINDOW_WIDTH, FmrbApp.WINDOW_HEIGHT, 0x60)
    gfx:draw_rect(0, 0, FmrbApp.WINDOW_WIDTH, 12, 0x60)
    gfx:draw_text("Lua App", 12, 2, FmrbGfx.WHITE)
end

-- Clear screen with BG clolor
gfx:clear(FmrbGfx.WHITE)
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

gfx:draw_text("Hello from Lua!", 3, 14, FmrbGfx.BLUE)
gfx:draw_text("FMRuby Graphics", 3, 24, FmrbGfx.BLACK)
gfx:draw_text(_VERSION, 3, 34, FmrbGfx.RED)
gfx:draw_text("Running: 0 s", 10, 54, 0x60)
gfx:present()

while running do
    -- Simple animation: update frame counter
    frame_count = frame_count + 1

    -- Update display every 60 frames (about 1 second at 60fps)
    if frame_count % 60 == 0 then
        local seconds = math.floor(frame_count / 60)
        -- Clear the counter area and redraw

        gfx:fill_rect(0, 54, FmrbApp.WINDOW_WIDTH-1, 64, FmrbGfx.WHITE)
        gfx:draw_text("Running: " .. tostring(seconds) .. "s", 10, 54, 0x60)
        draw_window_frame()
        gfx:present()
        print("Running: " .. tostring(seconds) .. "s")
    end

    -- Sleep for 16ms to maintain approximately 60fps
    FmrbApp.sleep(16)
end

print("Lua app execution completed")

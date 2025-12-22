-- Sample Lua Application for FMRuby
print("Hello from Lua!")
print("Lua version: " .. _VERSION)
print("Sample Lua app is running successfully")

-- Print window info from FmrbApp
print("Window size: " .. tostring(FmrbApp.WINDOW_WIDTH) .. "x" .. tostring(FmrbApp.WINDOW_HEIGHT))
print("Headless mode: " .. tostring(FmrbApp.HEADLESS))

-- Create canvas for this app
local canvas_id = FmrbApp.createCanvas(FmrbApp.WINDOW_WIDTH, FmrbApp.WINDOW_HEIGHT)
print("Canvas created: ID=" .. tostring(canvas_id))

-- Initialize graphics
local gfx = FmrbGfx.new(canvas_id)
print("Graphics initialized")

-- Clear screen with black
gfx:clear(FmrbGfx.COLOR_BLACK)

-- Draw some rectangles
-- gfx:fillRect(10, 10, 100, 50, FmrbGfx.COLOR_RED)
gfx:fillRect(200, 100, 300, 150, FmrbGfx.COLOR_RED)
gfx:fillRect(10, 70, 100, 50, FmrbGfx.COLOR_GREEN)
gfx:fillRect(10, 130, 100, 50, FmrbGfx.COLOR_BLUE)

-- Draw text
gfx:drawString("Hello from Lua!", 120, 20, FmrbGfx.COLOR_WHITE)
gfx:drawString("FMRuby Graphics", 120, 50, FmrbGfx.COLOR_YELLOW)
gfx:drawString("Lua " .. _VERSION, 120, 80, FmrbGfx.COLOR_CYAN)

-- Simple calculation test
local result = 10 + 20
local text = "10 + 20 = " .. tostring(result)
gfx:drawString(text, 120, 110, FmrbGfx.COLOR_MAGENTA)

-- Update screen
gfx:present(0, 0)

print("Graphics rendering completed")

-- Main loop to keep app running
print("Entering main loop...")
local running = true
local frame_count = 0

while running do
    -- Simple animation: update frame counter
    frame_count = frame_count + 1

    -- Update display every 60 frames (about 1 second at 60fps)
    if frame_count % 60 == 0 then
        local seconds = math.floor(frame_count / 60)
        -- Clear the counter area and redraw
        gfx:fillRect(10, 190, 200, 30, FmrbGfx.COLOR_BLACK)
        gfx:drawString("Running: " .. tostring(seconds) .. "s", 10, 200, FmrbGfx.COLOR_WHITE)
        gfx:present(0, 0)
        print("Running: " .. tostring(seconds) .. "s")
    end

    -- Sleep for 16ms to maintain approximately 60fps
    FmrbApp.sleep(16)
end

print("Lua app execution completed")

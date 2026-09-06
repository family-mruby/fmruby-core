# Ring-buffer viewport probe (Modern / P4 only).
#
# Isolates FmrbGfx#set_viewport torus compositing from the tile stamping the
# RPG demo layers on top of it: a 192x192 canvas gets twelve 16px columns in
# distinct colours (six drawn with fill_rect, six with draw_tile so the two
# paths can be told apart), then the viewport is stepped across the wrap
# point. Whatever is missing on screen names the culprit.
#
# Keys: right/left step the viewport by 16, up/down by 1. The status line
# shows the viewport origin so a screenshot is self-describing.

class RingProbeApp < FmrbApp
  BUF     = 192          # canvas size (12 tiles)
  CELL    = 16
  VIEW    = 176          # what the viewport shows
  ORIGIN_X = 16
  ORIGIN_Y = 16

  # One colour per column, so a missing or misplaced column is unmistakable.
  COLORS = [0xE0, 0x1C, 0x03, 0xFC, 0xE3, 0x1F,
            0xA0, 0x14, 0x02, 0xB4, 0x92, 0x6D]

  def on_create
    @vx = 0
    @vy = 0

    @map_gfx = create_canvas_gfx(width: BUF, height: BUF)

    # Columns 0-5 via fill_rect, columns 6-11 via a tile image, so that a
    # failure that belongs to one drawing path shows up as six columns.
    i = 0
    while i < 12
      x = i * CELL
      if i < 6
        @map_gfx.fill_rect(x, 0, CELL, BUF, COLORS[i])
      else
        # Two-tone column: solid top half, striped bottom, drawn cell by cell
        # the way TileSheet#stamp does it.
        y = 0
        while y < BUF
          @map_gfx.fill_rect(x, y, CELL, CELL, COLORS[i])
          @map_gfx.fill_rect(x + 4, y + 4, 8, 8, 0x00)
          y += CELL
        end
      end
      i += 1
    end

    @gfx.fill_rect(0, 0, @user_area_width, @user_area_height, FmrbGfx::BLACK)
    apply_view
    draw_status
  end

  def on_event(ev)
    return unless ev[:type] == :key_down
    case ev[:scancode]
    when 0x4F then @vx += CELL      # right
    when 0x50 then @vx -= CELL      # left
    when 0x52 then @vx += 1         # up   (fine step)
    when 0x51 then @vx -= 1         # down (fine step)
    else return
    end
    @vx = @vx % BUF
    apply_view
    draw_status
  end

  def on_update
    100
  end

  private

  def apply_view
    @map_gfx.set_viewport(@vx % BUF, @vy % BUF, VIEW, VIEW)
    @map_gfx.present(ORIGIN_X, ORIGIN_Y)
  end

  def draw_status
    x = ORIGIN_X + VIEW + 8
    @gfx.fill_rect(x, 0, @user_area_width - x, 40, FmrbGfx::BLACK)
    @gfx.draw_text(x, 8,  "view_x", FmrbGfx::WHITE)
    @gfx.draw_text(x, 20, "#{@vx}", FmrbGfx::WHITE)
    @gfx.present
  end
end

begin
  app = RingProbeApp.new
  app.start
rescue => e
  Log.error("RingProbe: #{e}")
end

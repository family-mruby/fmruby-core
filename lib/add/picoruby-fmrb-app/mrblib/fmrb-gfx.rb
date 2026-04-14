# FmrbGfx - Graphics API for Family mruby applications
# Core class is defined in C (gfx.c)
# This file provides Ruby-level wrapper methods

class FmrbGfx
  # Color constants (8-bit RGB332 format: 3-bit R, 3-bit G, 2-bit B)
  COLOR_BLACK   = 0x00  # R=0, G=0, B=0
  COLOR_WHITE   = 0xFF  # R=7, G=7, B=3
  COLOR_RED     = 0xE0  # R=7, G=0, B=0
  COLOR_GREEN   = 0x1C  # R=0, G=7, B=0
  COLOR_BLUE    = 0x03  # R=0, G=0, B=3
  COLOR_YELLOW  = 0xFC  # R=7, G=7, B=0
  COLOR_CYAN    = 0x1F  # R=0, G=7, B=3
  COLOR_MAGENTA = 0xE3  # R=7, G=0, B=3
  COLOR_GRAY    = 0x6D  # R=3, G=3, B=1

  # Blend modes for blend_rect
  BLEND_ADD = 0  # Per-component saturating add
  BLEND_XOR = 1  # Per-pixel XOR

  # Initialize graphics context
  # @param canvas_id [Integer] Canvas ID for this graphics instance
  # @param width [Integer] Canvas width (optional, for :center support)
  # @param height [Integer] Canvas height (optional, for :center support)
  def initialize(canvas_id, width: 0, height: 0)
    _init(canvas_id)
    @canvas_width = width
    @canvas_height = height
  end

  # Transfer a file from core to graphics-audio LittleFS
  # dest: destination path on graphics-audio (defaults to same as source path)
  def transfer_file(path, dest: nil)
    _transfer_file(path, dest || path)
  end

  # Check if a file exists on graphics-audio side
  # Returns Hash: {exists: bool, size: int}
  def file_status(path)
    _file_status(path)
  end

  # Create an image from a file on graphics-audio side
  # Returns Hash {id: image_id, width: w, height: h} or nil
  def create_image(path)
    _create_image_from_file(path)
  end

  # Draw a previously created image on the canvas
  # scale_x/scale_y: scaling factor (1.0 = original size, 2.0 = double, 0.5 = half)
  # When scale_y is 0.0, it uses the same value as scale_x (uniform scaling)
  def draw_image(image_id, x: 0, y: 0, scale_x: 1.0, scale_y: 0.0)
    _draw_image(image_id, x, y, scale_x, scale_y)
  end

  # Delete a previously created image
  def delete_image(image_id)
    _delete_image(image_id)
  end

  # High-level API: transfer if needed, create, draw, delete
  # coord: [x, y] array, :center symbol, or nil (defaults to [0,0])
  # mode: :fade_in (reserved for future use)
  def load_image(path, coord: nil, mode: nil)
    # Transfer file if not already on graphics-audio
    status = file_status(path)
    unless status[:exists]
      transfer_file(path)
    end

    # Create image from local file
    img = create_image(path)
    return if img.nil?

    img_id = img[:id]
    img_w = img[:width]
    img_h = img[:height]

    # Calculate position
    if coord == :center
      x = (@canvas_width - img_w) / 2
      y = (@canvas_height - img_h) / 2
    elsif coord
      x = coord[0]
      y = coord[1]
    else
      x = 0
      y = 0
    end

    draw_image(img_id, x: x, y: y)
    present
    delete_image(img_id)
  end
end

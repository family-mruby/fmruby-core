# SpriteImage - Sprite image buffer (reusable pixel data)
# Tied to a parent FmrbGfx instance. Destroyed when #destroy is called or GC'd.
class SpriteImage
  attr_reader :id, :width, :height

  # @param gfx [FmrbGfx] Parent graphics context
  # @param width [Integer] Image width in pixels
  # @param height [Integer] Image height in pixels
  # @param transparent_color [Integer] RGB332 color key (default: 0 = black)
  # @param use_transparent [Boolean] Enable color-key transparency
  def initialize(gfx, width:, height:, transparent_color: 0, use_transparent: false)
    @gfx = gfx
    @width = width
    @height = height
    @id = gfx._create_sprite_image(width, height,
                                    transparent_color,
                                    use_transparent ? 1 : 0)
  end

  # Set this image as drawing target.
  # After calling this, FmrbGfx draw methods (fill_rect, draw_circle, etc.)
  # will draw onto this sprite image.
  def set_target
    @gfx._set_sprite_image_target(@id)
  end

  # Reset drawing target back to the canvas
  def reset_target
    @gfx._set_sprite_image_target(0)
  end

  # Draw onto this sprite image with a block
  def draw
    set_target
    yield @gfx if block_given?
    reset_target
  end

  # Load a BMP file into this sprite image (fast: decoded on graphics-audio side)
  def load_bmp(path)
    @gfx._load_sprite_image_bmp(@id, path)
  end

  # Destroy this sprite image and free resources
  def destroy
    return unless @id
    @gfx._delete_sprite_image(@id)
    @id = nil
  end

  # In an app started from a checkpoint image (FmrbApp#_checkpoint_restored):
  # the id was the writing program's.
  def _checkpoint_forget
    @id = nil
  end
end

# SpriteInstance - Sprite placement (position, animation, visibility)
# References one or more SpriteImage frames for animation.
class SpriteInstance
  # x, y: window-local position. width, height: the first frame's size.
  attr_reader :id, :x, :y, :width, :height

  # @param gfx [FmrbGfx] Parent graphics context
  # @param images [SpriteImage, Array<SpriteImage>] Single image or animation frames
  # @param x [Integer] Window-local X position
  # @param y [Integer] Window-local Y position
  # @param z [Integer] Z-order within window (default: 0)
  def initialize(gfx, images, x:, y:, z: 0)
    @gfx = gfx
    image_list = images.is_a?(Array) ? images : [images]
    image_ids = image_list.map { |img| img.id }
    @id = gfx._create_sprite_instance(image_ids, x, y, z)
    @x = x
    @y = y
    @width = image_list[0].width
    @height = image_list[0].height
    @visible = true
    # Marked by FmrbApp#add_hit_sprite: part of a frameless window's hit
    # shape, moved along with the sprite (doc/frameless_window/ W3b).
    @_hit = false
    @_hit_mask = nil
  end

  # Move sprite to new position
  def move(x, y)
    @x = x
    @y = y
    @gfx._sprite_move(@id, x, y)
    FmrbApp._hit_sprite_place(@id, x, y, @visible ? 1 : 0) if @_hit && @id
  end

  # Set visibility
  def visible=(v)
    @visible = v ? true : false
    @gfx._sprite_visible(@id, v)
    FmrbApp._hit_sprite_place(@id, @x, @y, @visible ? 1 : 0) if @_hit && @id
  end

  def visible?
    @visible
  end

  # Set animation frame index
  def frame=(index)
    @gfx._sprite_frame(@id, index)
  end

  # Destroy this sprite instance
  def destroy
    return unless @id
    FmrbApp._hit_sprite_place(@id, @x, @y, 0) if @_hit
    @gfx._delete_sprite_instance(@id)
    @id = nil
  end

  # FmrbApp#add_hit_sprite / #remove_hit_sprite.
  def _hit_mark(on, mask)
    @_hit = on
    @_hit_mask = mask
  end

  def _hit_mask
    @_hit_mask
  end

  # In an app started from a checkpoint image (FmrbApp#_checkpoint_restored):
  # the id was the writing program's.
  def _checkpoint_forget
    @id = nil
  end
end

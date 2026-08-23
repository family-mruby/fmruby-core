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

  # Currently selected font and text size. Tracked here (not on the WROVER
  # side) so the window frame can save and restore the app's choice
  # without an extra round trip.
  attr_reader :current_font, :current_text_size

  # Canvas dimensions captured at construction time. Exposed so wrapper
  # libraries (e.g. P5) can query the drawable area without an extra
  # round trip to the graphics board.
  attr_reader :canvas_width, :canvas_height
  attr_reader :canvas_id

  # Pixel metrics for the fonts currently supported by the graphics
  # backend (LovyanGFX on the WROVER side). All supported fonts are
  # fixed-pitch, so width and height per glyph are constants.
  #   :default      Font0 (6x8 ASCII)
  #   [:ja, 8]      misaki_8 (8x8, includes BMP CJK)
  #   [:ja, 12]     efontJA_12 (12x12)
  # char_w is the full-width (CJK) cell, half_w the ASCII cell. Both :ja fonts
  # are dual width -- misaki draws ASCII at 4px and kanji at 8, efont at 6 and
  # 12 -- and treating ASCII as full width made every measurement of a mixed
  # string too wide, which is what places a menu label or a truncation point.
  #
  # kana_w is half-width katakana (U+FF61-U+FF9F) and is NOT simply half_w:
  # misaki has those glyphs and draws them in a half cell, efontJA_12 does not
  # and falls back to a full-width box. Measured on the simulator with
  # flash/app/test/ja_width.app.rb, "ｱｲｳｴｵ|": misaki 26px (5x4 + 6),
  # efont 66px (5x12 + 6) -- the same as five kanji.
  FONT_METRICS = {
    [:default]     => { char_w: 6,  half_w: 6, kana_w: 4,  line_h: 8  },
    [:ja, 8]       => { char_w: 8,  half_w: 4, kana_w: 4,  line_h: 8  },
    [:ja, 12]      => { char_w: 12, half_w: 6, kana_w: 12, line_h: 12 },
    [:ja, 16]      => { char_w: 16, half_w: 8, kana_w: 16, line_h: 16 },
    # The bold cut is the same metal-width as the regular one: efont is
    # fixed-pitch and the bold is drawn in the same cell.
    [:ja_bold, 12] => { char_w: 12, half_w: 6, kana_w: 12, line_h: 12 },
  }

  # What each machine carries. Fonts live in the display's flash, so what can
  # be drawn is a property of the machine, not of the app. Both families
  # carry the same set today -- the two extra efont cuts cost 758KB on the
  # WROVER and its 2000K app partition still has 437KB free with them in --
  # but Retro is the one with no room to spare, so the two rows stay apart
  # and this is where a cut gets dropped if that changes. Dropping one here
  # means dropping it in the display too (FMRB_FONT_JA_EXTRA in
  # graphics_handler.cpp).
  #
  # An app names the font it wants and gets the nearest one this machine has;
  # set_font answers with what it actually selected, so a caller that cares --
  # one drawing its own bold, say -- can tell.
  #
  # The simulator follows the table of the family it stands in for, not what
  # LovyanGFX happens to have compiled in, so a deck checked in a Retro sim
  # looks like Retro.
  FONT_AVAILABLE = {
    "modern" => [[:default], [:ja, 8], [:ja, 12], [:ja, 16], [:ja_bold, 12]],
    "retro"  => [[:default], [:ja, 8], [:ja, 12], [:ja, 16], [:ja_bold, 12]],
  }

  # Nearest thing this machine has to the font asked for: a size it does not
  # carry falls back to 12, and a bold it does not carry to the regular cut.
  def self.resolve_font(key)
    have = FONT_AVAILABLE[FmrbConst::HW_FAMILY] || FONT_AVAILABLE["retro"]
    return key if have.include?(key)
    family = key[0]
    if family == :ja_bold
      plain = [:ja, key[1]]
      return plain if have.include?(plain)
      return [:ja, 12] if have.include?([:ja, 12])
      return [:default]
    end
    if family == :ja
      return [:ja, 12] if have.include?([:ja, 12])
      return [:default]
    end
    [:default]
  end

  # Initialize graphics context
  # @param canvas_id [Integer] Canvas ID for this graphics instance
  # @param width [Integer] Canvas width (optional, for :center support)
  # @param height [Integer] Canvas height (optional, for :center support)
  def initialize(canvas_id, width: 0, height: 0)
    _init(canvas_id)
    @canvas_id = canvas_id
    @canvas_width = width
    @canvas_height = height
    # LovyanGFX boots a fresh target with Font0 and text size 1.
    @current_font = [:default]
    @current_text_size = 1
  end

  # Select the font family for subsequent draw_text calls on this canvas.
  #
  # The font asked for may not be on this machine (see FONT_AVAILABLE), in
  # which case the nearest one is selected instead. Returns what was actually
  # selected as [family, size] (or [:default]), which is also what
  # text_width and font_height then measure.
  #
  # @param family [Symbol] :default (Font0 6x8 ASCII), :ja (Japanese) or
  #   :ja_bold (the bold cut of :ja)
  # @param size [Integer, nil] Pixel height. :ja has 8 (misaki), 12 and 16
  #   (efontJA); :ja_bold has 12. Ignored for :default.
  def set_font(family, size = nil)
    key = FmrbGfx.resolve_font(family == :default ? [:default]
                                                  : [family, (size || 8)])
    @current_font = key
    key.length > 1 ? _set_font(key[0], key[1]) : _set_font(key[0])
    key
  end

  # Set the text scale multiplier (1..4). Applies on top of the active font.
  def set_text_size(size)
    @current_text_size = size
    _set_text_size(size)
    self
  end

  # Draw a single line of text at (x, y).
  # @param mixed [Boolean] When true, ASCII bytes render with the system
  #   Font0 (6x8) and UTF-8 multi-byte runs render with misaki_8 (8x8).
  #   The current_font selection is preserved on either side of the call.
  def draw_text(x, y, str, color, bg_color = nil, mixed: false)
    if mixed
      if bg_color
        _draw_text_hybrid(x, y, str, color, bg_color)
      else
        _draw_text_hybrid(x, y, str, color)
      end
    else
      if bg_color
        _draw_text(x, y, str, color, bg_color)
      else
        _draw_text(x, y, str, color)
      end
    end
    self
  end

  # Positional-argument form of draw_text(..., mixed: true).
  #
  # Keyword arguments build a Hash on every call in mruby, which is not
  # acceptable on a redraw path that must not allocate (FmrbUI#flush). This
  # form renders ASCII with Font0 (6x8) and multi-byte UTF-8 runs with
  # misaki_8 (8x8) without touching the current font selection.
  def draw_text_mixed(x, y, str, color, bg_color = nil)
    if bg_color
      _draw_text_hybrid(x, y, str, color, bg_color)
    else
      _draw_text_hybrid(x, y, str, color)
    end
    self
  end

  # Compute the rendered pixel width of a string with the given font.
  # When family/size are omitted, the currently selected font is used.
  # Multi-byte UTF-8 glyphs are counted as full-width (char_w * 2) for
  # the :default font, because Font0 has no CJK glyphs and apps that
  # mix ASCII and CJK typically render the CJK portion with misaki_8
  # at twice the cell width.
  def text_width(str, family = nil, size = nil)
    key = font_key(family, size)
    metrics = FONT_METRICS[key] || FONT_METRICS[[:default]]
    char_w = metrics[:char_w]
    half_w = metrics[:half_w] || char_w
    kana_w = metrics[:kana_w] || char_w
    # Font0 has no CJK glyphs, so the default font renders a multi-byte run
    # hybrid with misaki_8; count that at 8px to match what is drawn.
    wide_w = (key == [:default]) ? 8 : char_w
    bytes = str.bytes
    width = 0
    i = 0
    n = bytes.length
    while i < n
      b = bytes[i]
      if b < 0x80
        width += half_w
        i += 1
      elsif b < 0xC0
        # Stray continuation byte: skip
        i += 1
      else
        seq_len = if b < 0xE0 then 2
                  elsif b < 0xF0 then 3
                  else 4
                  end
        # Half-width katakana (U+FF61-U+FF9F) is EF BD A1..EF BE 9F and draws
        # in a half cell like ASCII, not a full one.
        b1 = (i + 1 < n) ? bytes[i + 1] : 0
        b2 = (i + 2 < n) ? bytes[i + 2] : 0
        halfkana = (seq_len == 3 && b == 0xEF &&
                    ((b1 == 0xBD && b2 >= 0xA1) || (b1 == 0xBE && b2 <= 0x9F)))
        width += halfkana ? kana_w : wide_w
        i += seq_len
      end
    end
    width * @current_text_size
  end

  # Glyph cell height in pixels for the given font (current font by default),
  # multiplied by the current text size.
  def font_height(family = nil, size = nil)
    key = font_key(family, size)
    metrics = FONT_METRICS[key] || FONT_METRICS[[:default]]
    metrics[:line_h] * @current_text_size
  end

  # Read a single RGB332 pixel back from this canvas. Synchronous round
  # trip to the graphics backend, so use sparingly in tight loops.
  # Returns 0 when (x, y) is outside the canvas bounds.
  def get_pixel(x, y)
    _get_pixel(x, y)
  end

  # Upload a 1bpp mask to the graphics backend.
  # @param width  [Integer]
  # @param height [Integer]
  # @param data   [String] binary string, ceil(width/8)*height bytes,
  #   MSB-first per byte. 1 bit = pixel drawn, 0 bit = transparent.
  # @return [Integer] mask_id, for use with draw_image_masked / delete_mask
  def create_mask(width, height, data)
    _create_mask(width, height, data)
  end

  # Release a mask uploaded with create_mask. Safe to call from any task;
  # routed through the host queue so it stays ordered with preceding
  # draw_image_masked calls that may still reference the mask.
  def delete_mask(mask_id)
    _delete_mask(mask_id)
  end

  # Blit a SpriteImage onto this canvas using a 1bpp mask cutout. Pixels
  # are sampled from the SpriteImage at local (xx, yy) and written at
  # (x+xx, y+yy) only where the mask bit is set.
  # @param image_id [Integer] SpriteImage id (from SpriteImage#id)
  # @param mask_id  [Integer] mask id (from create_mask)
  # @param x [Integer]
  # @param y [Integer]
  def draw_image_masked(image_id, mask_id, x:, y:)
    _draw_image_masked(image_id, mask_id, x, y)
  end

  # Stamp the sub-region (src_x, src_y, w, h) of a SpriteImage onto this
  # canvas at (dst_x, dst_y). Source pixels equal to the SpriteImage's
  # transparent_color (when use_transparent is set) are skipped. No
  # SpriteInstance is allocated; the stamp is a one-shot canvas write.
  # Designed for stateless BG tile rendering.
  # @param image_id [Integer] SpriteImage id (typically a tilesheet)
  # @param src_x [Integer] source rect top-left x inside the SpriteImage
  # @param src_y [Integer] source rect top-left y
  # @param w [Integer] source rect width (= dest width)
  # @param h [Integer] source rect height
  # @param dst_x [Integer] destination x on this canvas
  # @param dst_y [Integer] destination y on this canvas
  def draw_tile(image_id, src_x, src_y, w, h, dst_x:, dst_y:)
    _draw_tile(image_id, src_x, src_y, w, h, dst_x, dst_y)
  end

  # Draw a thick line by stacking parallel 1-pixel lines along the
  # perpendicular direction. The graphics backend has no native thick-line
  # primitive, so this is implemented in Ruby. Aliased on top of draw_line.
  def draw_thick_line(x0, y0, x1, y1, thickness, color)
    t = thickness.to_i
    if t <= 1
      draw_line(x0, y0, x1, y1, color)
      return self
    end
    dx = x1 - x0
    dy = y1 - y0
    len_sq = dx * dx + dy * dy
    if len_sq == 0
      # Degenerate: draw a small filled square centered on (x0,y0)
      fill_rect(x0 - t / 2, y0 - t / 2, t, t, color)
      return self
    end
    len = Math.sqrt(len_sq)
    nx = -dy.to_f / len
    ny =  dx.to_f / len
    half = (t - 1) / 2
    (-half..(t - 1 - half)).each do |k|
      ox = (nx * k).round
      oy = (ny * k).round
      draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color)
    end
    self
  end

  # Make sure a file on graphics-audio matches the local one, transferring it
  # only when it differs (size + CRC32). Use this for assets: checking
  # file_status[:exists] instead leaves an edited asset stale forever.
  # dest: destination path on graphics-audio (defaults to same as source path)
  def sync_file(path, dest: nil)
    _sync_file(path, dest || path)
  end

  # Transfer a file from core to graphics-audio LittleFS unconditionally.
  # Prefer sync_file unless you know the destination has to be rewritten.
  # dest: destination path on graphics-audio (defaults to same as source path)
  def transfer_file(path, dest: nil)
    _transfer_file(path, dest || path)
  end

  # Check if a file exists on graphics-audio side
  # Returns Hash: {exists: bool, size: int}
  def file_status(path)
    _file_status(path)
  end

  # Write the picture the last present put on screen to a file, on the display
  # side's filesystem. This does not present: send present first, then this,
  # and the two keep their order.
  #
  # Tab5 writes a JPEG through the SoC's encoder, into the filesystem the core
  # shares with it (so File.exist? can tell when it is done). The simulator
  # writes a BMP into the graphics side's own storage, which the core cannot
  # see. Retro (WROVER) does not support it and says so in its log.
  def export_frame(path)
    _export_frame(path)
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
    sync_file(path)

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

  # Replace this canvas's composite region list. The compositor on the
  # graphics-audio side copies only the listed sub-rects each frame instead
  # of pushing the whole canvas; each region picks its own transparent /
  # opaque mode independently. Pass nil or [] to restore the default
  # full-area pushSprite path.
  #
  # Each region is a Hash:
  #   {
  #     src_x:, src_y:,        # source top-left within the canvas (optional;
  #                            #   defaults to dst_x / dst_y for the common
  #                            #   case where source == destination)
  #     dst_x:, dst_y:,        # destination offset relative to canvas origin
  #                            #   (defaults to 0, 0)
  #     w:, h:,                # region width / height in pixels
  #     transparent:           # true = per-pixel color-key compare,
  #                            #   false = opaque memcpy fast path
  #   }
  #
  # Up to 8 regions can be set in one call (graphics-audio buffer cap).
  def set_composite_regions(regions)
    if regions.nil? || regions.empty?
      _set_composite_regions([])
      return self
    end

    flat = []
    regions.each do |r|
      dst_x = r[:dst_x] || 0
      dst_y = r[:dst_y] || 0
      src_x = r[:src_x] || dst_x
      src_y = r[:src_y] || dst_y
      w = r[:w] || 0
      h = r[:h] || 0
      trans = r[:transparent] ? 1 : 0
      flat << src_x << src_y << dst_x << dst_y << w << h << trans
    end
    _set_composite_regions(flat)
    self
  end

  # Composite source viewport (hardware scroll register, Modern/P4 only):
  # show only the (src_x, src_y, w, h) sub-rect of this canvas at its push
  # position. The canvas is addressed as a torus (the source rect wraps
  # around the canvas edges), so a ring-buffer canvas slightly larger than
  # the viewport can scroll an arbitrarily large world: pass the world
  # scroll offset directly and stamp newly exposed tiles as it moves (see
  # TileRing). Gate usage on FmrbConst::CHIP_MODEL == "ESP32-P4"; the
  # Retro backend ignores this command.
  def set_viewport(src_x, src_y, w, h)
    _set_canvas_viewport(src_x, src_y, w, h)
    self
  end

  # Confine this canvas's sprites to the (x, y, w, h) sub-rect. Sprites are
  # composited on top of everything the canvas drew, so by default they paint
  # over the window frame and title bar the app drew into the same canvas.
  # The rect uses sprite coordinates (the same space passed to
  # SpriteInstance#move) and is clamped to the canvas. FmrbApp already sets
  # this to the user area for windowed apps; call it to narrow the area
  # further, e.g. to keep a game's sprites out of its own score bar.
  def set_sprite_clip(x, y, w, h)
    _set_sprite_clip(x, y, w, h)
    self
  end

  # Let sprites use the whole canvas again (clears the clip).
  def clear_sprite_clip
    _set_sprite_clip(0, 0, 0, 0)
    self
  end

  # Play a file of concatenated JPEG frames into this canvas at (x, y).
  # Modern (Tab5) only; returns nil anywhere else, so callers can fall back.
  # Whatever the app drew elsewhere on the canvas stays; do not draw inside
  # the picture rect while it plays.
  def video_open(path, x: 0, y: 0, fps: 15, loop: false)
    info = _video_open(path, x, y, fps, loop)
    return nil if info.nil?
    FmrbVideo.new(self, info[:width], info[:height])
  end

  private

  # Normalize a (family, size) pair against the current selection so the
  # metrics lookup matches the FONT_METRICS key used at set_font time.
  def font_key(family, size)
    return @current_font if family.nil?
    family == :default ? [:default] : [family, (size || 8)]
  end
end

# Handle for a motion-JPEG file being played into a canvas (FmrbGfx#video_open).
# One player exists at a time, so every instance talks to the same one.
class FmrbVideo
  # Actions and states are the raw protocol numbers:
  #   action 0 play / 1 pause / 2 stop / 3 rewind
  #   state  0 idle / 1 playing / 2 paused / 3 finished

  def initialize(gfx, width, height)
    @gfx = gfx
    @width = width
    @height = height
  end

  def width
    @width
  end

  def height
    @height
  end

  def play
    @gfx._video_control(0)
    self
  end

  def pause
    @gfx._video_control(1)
    self
  end

  # Stops and releases the file. The canvas keeps the last frame drawn.
  def stop
    @gfx._video_control(2)
    self
  end

  def rewind
    @gfx._video_control(3)
    self
  end

  # {state:, shown:, dropped:} or nil
  def status
    @gfx._video_status
  end

  def playing?
    st = @gfx._video_status
    st ? st[:state] == 1 : false
  end

  def finished?
    st = @gfx._video_status
    st ? st[:state] == 3 : false
  end
end

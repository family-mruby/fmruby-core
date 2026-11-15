# FmrbGfx - Graphics API for Family mruby applications
# Core class is defined in C (gfx.c). The methods on the app boundary
# (sig/fmrb_gfx.rbs, doc/app_boundary) are generated C methods; the ones
# here are the Ruby wrappers: optional and keyword arguments over the
# simple forms named *_raw, and what the object itself still keeps (the
# font and the canvas size). The canvas, the drawing origin and the area are
# the C side's (its drawer): canvas_id, origin_x ... read them from there.
#
# FmrbGfx draws, and that includes the text helpers mixed in from FmrbText
# (fit_text, wrap_text, draw_text_lines) and the rounded button picture
# (draw_button). What can be clicked is FmrbUI's: its widgets, or
# FmrbUI::HitAreas for a screen redrawn every time
# (doc/reference/screen_helpers.md).

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

  # Canvas dimensions captured at construction time (the whole canvas: for
  # an app's window, frame included).
  attr_reader :canvas_width, :canvas_height
  # The canvas, the drawing origin and the size of the area it starts are
  # the C side's (canvas_id, origin_x, origin_y, area_width, area_height, on
  # the app boundary). Every coordinate a draw call takes is from that
  # origin, which the firmware adds: the app's @gfx starts at the top-left
  # of the area the app owns (inside the frame), anything else at the
  # canvas's (doc/codebase_tidy T4).

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

  # A build without the efontJA fonts (FMRB_FONT_JA=0, FmrbConst::FONT_JA is
  # false) has misaki 8 alone, whatever the family.
  FONT_AVAILABLE_NO_JA = [[:default], [:ja, 8]]

  # Nearest thing this machine has to the font asked for: a size it does not
  # carry falls back to 12 (or to misaki 8 when that is all there is), and a
  # bold it does not carry to the regular cut.
  def self.resolve_font(key)
    have = FONT_AVAILABLE[FmrbConst::HW_FAMILY] || FONT_AVAILABLE["retro"]
    have = FONT_AVAILABLE_NO_JA unless FmrbConst::FONT_JA
    return key if have.include?(key)
    family = key[0]
    if family == :ja_bold
      plain = [:ja, key[1]]
      return plain if have.include?(plain)
    end
    if family == :ja || family == :ja_bold
      return [:ja, 12] if have.include?([:ja, 12])
      return [:ja, 8] if have.include?([:ja, 8])
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
    _set_origin(0, 0, width, height)
    @canvas_width = width
    @canvas_height = height
    # LovyanGFX boots a fresh target with Font0 and text size 1.
    @current_font = [:default]
    @asked_font = [:default]
    @current_text_size = 1
  end

  # The canvas this object draws on has another size now (a restored app's
  # window on another screen, doc/checkpoint F4): what :center measures.
  def _canvas_size(width, height)
    @canvas_width = width
    @canvas_height = height
    nil
  end

  # An extra canvas of the app (FmrbApp#create_canvas_gfx): how it was made,
  # which a checkpoint image carries to make it again (doc/checkpoint F3).
  def _extra_canvas(z_offset, transparent, key)
    @_extra = [z_offset, transparent, key]
    nil
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
    asked = family == :default ? [:default] : [family, (size || 8)]
    key = FmrbGfx.resolve_font(asked)
    @asked_font = asked
    @current_font = key
    set_font_raw(key[0], key.length > 1 ? key[1] : 12)
    key
  end

  # The font last asked for with set_font, before this machine picked the
  # nearest it has (current_font). What the base saves and puts back around
  # the window frame, and what a moved app's font is picked from again on the
  # machine it lands on (doc/checkpoint/report/font_pct.md): passing through
  # a machine without the efontJA fonts must not leave [:ja, 8] behind. An
  # image written before this was kept has no @asked_font: current_font then.
  def _asked_font
    @asked_font || @current_font
  end

  # A moved app's canvas, bound again on this machine (FmrbApp's
  # _checkpoint_restored): its font picked again from what was asked for and
  # its text scale sent again (the display side starts this canvas afresh).
  def _checkpoint_font
    font = _asked_font
    set_font(*font) if font && font != [:default]
    size = @current_text_size
    set_text_size(size) if size && size != 1
    nil
  end

  # Set the text scale multiplier (1..4). Applies on top of the active font.
  def set_text_size(size)
    @current_text_size = size
    set_text_size_raw(size)
    self
  end

  # Draw a single line of text at (x, y).
  # @param mixed [Boolean] When true, ASCII bytes render with the system
  #   Font0 (6x8) and UTF-8 multi-byte runs render with misaki_8 (8x8).
  #   The current_font selection is preserved on either side of the call.
  def draw_text(x, y, str, color, bg_color = nil, mixed: false)
    draw_text_raw(x, y, str, color, bg_color || -1, mixed)
  end

  # Positional-argument form of draw_text(..., mixed: true).
  #
  # Keyword arguments build a Hash on every call in mruby, which is not
  # acceptable on a redraw path that must not allocate (FmrbUI#flush). This
  # form renders ASCII with Font0 (6x8) and multi-byte UTF-8 runs with
  # misaki_8 (8x8) without touching the current font selection.
  def draw_text_mixed(x, y, str, color, bg_color = nil)
    draw_text_raw(x, y, str, color, bg_color || -1, true)
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

  # A rounded button: filled with fill, label centred in fg with the current
  # font. Only the picture -- the click target is the app's
  # (FmrbUI::HitAreas#add), so a screen redrawn every time draws a button
  # and adds its target in two lines.
  def draw_button(x, y, w, h, label, fill, fg, radius = 3)
    fill_round_rect(x, y, w, h, radius, fill)
    draw_text(x + (w - text_width(label)) / 2, y + (h - font_height) / 2, label, fg)
    self
  end

  # get_pixel, create_mask and delete_mask are on the boundary as they are
  # (generated in gfx.c; sig/fmrb_gfx.rbs).

  # Blit a SpriteImage onto this canvas using a 1bpp mask cutout. Pixels
  # are sampled from the SpriteImage at local (xx, yy) and written at
  # (x+xx, y+yy) only where the mask bit is set.
  # @param image_id [Integer] SpriteImage id (from SpriteImage#id)
  # @param mask_id  [Integer] mask id (from create_mask)
  # @param x [Integer]
  # @param y [Integer]
  def draw_image_masked(image_id, mask_id, x:, y:)
    draw_image_masked_raw(image_id, mask_id, x, y)
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
    draw_tile_raw(image_id, src_x, src_y, w, h, dst_x, dst_y)
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
    sync_file_raw(path, dest || path)
  end

  # Transfer a file from core to graphics-audio LittleFS unconditionally.
  # Prefer sync_file unless you know the destination has to be rewritten.
  # dest: destination path on graphics-audio (defaults to same as source path)
  def transfer_file(path, dest: nil)
    transfer_file_raw(path, dest || path)
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

  # Draw a previously created image on the canvas
  # scale_x/scale_y: scaling factor (1.0 = original size, 2.0 = double, 0.5 = half)
  # When scale_y is 0.0, it uses the same value as scale_x (uniform scaling)
  def draw_image(image_id, x: 0, y: 0, scale_x: 1.0, scale_y: 0.0)
    draw_image_raw(image_id, x, y, scale_x, scale_y)
  end

  # file_status, create_image and delete_image are on the boundary as they
  # are (generated in gfx.c).

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
      x = (area_width - img_w) / 2
      y = (area_height - img_h) / 2
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
      set_composite_regions_raw([])
      return self
    end

    list = []
    regions.each do |r|
      dst_x = r[:dst_x] || 0
      dst_y = r[:dst_y] || 0
      list << { src_x: r[:src_x] || dst_x, src_y: r[:src_y] || dst_y, dst_x: dst_x, dst_y: dst_y,
                w: r[:w] || 0, h: r[:h] || 0, transparent: r[:transparent] ? true : false }
    end
    set_composite_regions_raw(list)
    self
  end

  # set_viewport / clear_viewport (Modern only: the hardware scroll register,
  # the canvas addressed as a torus) and set_sprite_clip / clear_sprite_clip
  # (sprite coordinates, from the drawing origin) are on the boundary as they
  # are (generated in gfx.c; sig/fmrb_gfx.rbs has their help).

  # present([x, y]), the wrapper of present_raw, is a C method (gfx.c): an
  # app calls it every frame (doc/app_boundary, decided after W4b-1).

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

# frozen_string_literal: true
#
# recorder.rb - FmrbGfx's C half, as a recorder.
#
# The Ruby half (lib/gems/picoruby-fmrb-app/mrblib/fmrb-gfx.rb: draw_text,
# set_font, text_width with FONT_METRICS, ...) is loaded as it ships; the
# methods gfx.c defines are defined here and write one line per call into
# the record, in the order called. A text line carries the font and the
# text size in effect (not the set_font calls themselves), so a change that
# only sets the same font again does not show; a change of what is drawn
# does.
module Regress
  module Rec
    class << self
      attr_reader :lines

      def reset
        @lines = []
        @last_present = false
      end

      def add(line)
        @lines << line
        @last_present = false
      end

      # Consecutive presents with nothing between show one picture.
      def present(prefix)
        return if @last_present
        @lines << "#{prefix}present"
        @last_present = true
      end

      def mark(line)
        @lines << "== #{line}"
        @last_present = false
      end
    end
    reset
  end

  module Fmt
    module_function

    def v(x)
      case x
      when Float then format("%.2f", x)
      when Integer, NilClass, TrueClass, FalseClass, Symbol then x.inspect
      when String then x.inspect
      when Array then "[#{x.map { |e| v(e) }.join(',')}]"
      when Hash then "{#{x.map { |k, e| "#{k}:#{v(e)}" }.join(',')}}"
      else x.class.name
      end
    end

    def c(x) = x.is_a?(Integer) ? format("#%02X", x & 0xFF) : v(x)

    def args(a) = a.map { |e| v(e) }.join(" ")
  end
end

class FmrbGfx
  GFXVM_OP_CLEAR = 1
  GFXVM_OP_FILL_RECT = 2
  GFXVM_OP_DRAW_RECT = 3
  GFXVM_OP_FILL_ROUND_RECT = 4
  GFXVM_OP_DRAW_ROUND_RECT = 5
  GFXVM_OP_DRAW_LINE = 6
  GFXVM_OP_FILL_CIRCLE = 7
  GFXVM_OP_DRAW_TEXT = 8

  BLACK = 0x00
  WHITE = 0xFF
  RED = 0xE0
  GREEN = 0x1C
  BLUE = 0x03
  YELLOW = 0xFC
  CYAN = 0x1F
  MAGENTA = 0xE3
  GRAY = 0x6D

  def self.rgb_to_332(r, g, b) = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)

  def self.hsv_to_rgb(h, s, v)
    h = h.to_f % 360
    s = s.to_f
    v = v.to_f
    s /= 255.0 if s > 1
    v /= 255.0 if v > 1
    c = v * s
    x = c * (1 - ((h / 60.0) % 2 - 1).abs)
    m = v - c
    r, g, b = case (h / 60).floor
              when 0 then [c, x, 0]
              when 1 then [x, c, 0]
              when 2 then [0, c, x]
              when 3 then [0, x, c]
              when 4 then [x, 0, c]
              else [c, 0, x]
              end
    [((r + m) * 255).round, ((g + m) * 255).round, ((b + m) * 255).round]
  end

  def _init(id)
    @canvas_id = id
    @__prefix = id == 1 ? "" : "[c#{id}] "
    @__size = 1
    @__font = [:default]
    @__ox = @__oy = @__aw = @__ah = 0
    @__image = false
  end

  # The drawing origin gfx.c keeps (doc/codebase_tidy T4): what a draw call
  # adds to its coordinates, so the record holds the canvas's, as the
  # display side gets them.
  def _set_origin(x, y, w, h) = (@__ox, @__oy, @__aw, @__ah = x, y, w, h; self)
  def canvas_id = @canvas_id
  def origin_x = @__ox
  def origin_y = @__oy
  def area_width = @__aw
  def area_height = @__ah
  def __dx = @__image ? 0 : @__ox
  def __dy = @__image ? 0 : @__oy

  # Coordinates in a list of arguments: which ones are x and which y.
  def __at(a, xs, ys)
    a = a.dup
    xs.each { |i| a[i] += __dx if a[i].is_a?(Numeric) }
    ys.each { |i| a[i] += __dy if a[i].is_a?(Numeric) }
    a
  end

  def __rec(name, *a) = Regress::Rec.add("#{@__prefix}#{name} #{Regress::Fmt.args(a)}".rstrip)

  def __font_tag
    f = @__font.length > 1 ? "#{@__font[0]}#{@__font[1]}" : @__font[0].to_s
    "#{f}x#{@__size}"
  end

  # The methods on the app boundary are generated C methods in the firmware
  # (sig/fmrb_gfx.rbs, doc/app_boundary); these stand in for them.
  def set_font_raw(family, size) = (@__font = family == :default ? [:default] : [family, size]; self)
  def set_text_size_raw(n) = (@__size = n; self)

  # An empty text draws nothing (and is not recorded). mixed: Font0 for ASCII
  # and misaki 8 for the rest, whatever the font. A negative bg is none.
  def draw_text_raw(x, y, s, c, bg, mixed)
    return self if s.to_s.empty?
    x += __dx
    y += __dy
    t = "#{@__prefix}#{mixed ? 'textm' : 'text'} #{Regress::Fmt.v(x)} #{Regress::Fmt.v(y)} #{s.to_s.inspect} #{Regress::Fmt.c(c)}"
    t += " bg=#{Regress::Fmt.c(bg)}" if bg >= 0
    Regress::Rec.add(mixed ? "#{t} x#{@__size}" : "#{t} #{__font_tag}")
    self
  end

  # Shapes: the coordinates, then the colour last.
  %i[set_pixel draw_line draw_rect fill_rect draw_circle fill_circle draw_round_rect fill_round_rect
     draw_ellipse fill_ellipse draw_triangle fill_triangle draw_arc fill_arc].each do |m|
    pts = %i[draw_line draw_triangle fill_triangle].include?(m) ? (m == :draw_line ? 2 : 3) : 1
    define_method(m) do |*a|
      a = __at(a, (0...pts).map { |k| 2 * k }, (0...pts).map { |k| 2 * k + 1 })
      Regress::Rec.add("#{@__prefix}#{m} #{Regress::Fmt.args(a[0..-2])} #{Regress::Fmt.c(a[-1])}")
      self
    end
  end

  def blend_rect(*a) = (__rec(:blend_rect, *__at(a, [0], [1])); self)
  def clear(c = 0) = (Regress::Rec.add("#{@__prefix}clear #{Regress::Fmt.c(c)}"); self)
  def present_raw(_x, _y, _positioned) = (Regress::Rec.present(@__prefix); self)
  # present([x, y]) is a C method in the firmware too (gfx.c), not mrblib.
  def present(_x = nil, _y = nil) = (Regress::Rec.present(@__prefix); self)
  def destroy = (__rec(:destroy); nil)
  def draw_wallclock(*a) = (__rec(:draw_wallclock, *__at(a, [0], [1])); self)
  def draw_free_iram(*a) = (__rec(:draw_free_iram, *__at(a, [0], [1])); self)
  def set_output_level(*) = self
  def set_chroma_level(*) = self
  def get_pixel(_x, _y) = 0

  def sync_file_raw(_path, _dest) = true
  def transfer_file_raw(_path, _dest) = true

  def file_status(path)
    host = Regress::FakeFS.read_path(path)
    host ? { exists: true, size: File.__regress_size(host) } : { exists: false, size: 0 }
  end

  def __next_id = (Regress::Env.ids += 1)

  def create_image(path)
    w, h = Regress::Env.image_size(path)
    return nil unless w
    id = __next_id
    __rec(:create_image, id, path)
    { id: id, width: w, height: h }
  end

  # The head of a picture file (FmrbGfx's picture store asks before it
  # reads one onto the display side).
  def picture_info(path)
    w, h = Regress::Env.image_size(path)
    return nil unless w
    host = Regress::FakeFS.read_path(path)
    { width: w, height: h, format: 1, bytes: host ? File.__regress_size(host) : 0, mtime: 0 }
  end

  def draw_image_raw(id, x, y, sx, sy) = (__rec(:draw_image, id, x + __dx, y + __dy, sx, sy); self)
  def delete_image(id) = (__rec(:delete_image, id); self)
  def create_mask(w, h, _data) = (id = __next_id; __rec(:create_mask, id, w, h); id)
  def delete_mask(id) = (__rec(:delete_mask, id); self)
  def draw_image_masked_raw(img, mask, x, y) = (__rec(:draw_image_masked, img, mask, x + __dx, y + __dy); self)
  def draw_tile_raw(id, sx, sy, w, h, dx, dy) = (__rec(:draw_tile, id, sx, sy, w, h, dx + __dx, dy + __dy); self)
  def draw_tile_scaled_raw(id, sx, sy, w, h, dx, dy, k, cx, cy, cw, ch)
    clip = cw > 0 && ch > 0 ? [cx + __dx, cy + __dy, cw, ch] : [0, 0, 0, 0]
    __rec(:draw_tile_scaled, id, sx, sy, w, h, dx + __dx, dy + __dy, k, *clip)
    self
  end
  def video_open_raw(path, x, y, fps, lp) = (__rec(:video_open, path, x + __dx, y + __dy, fps, lp); Regress::Env.video_info)
  # One player for the whole app, so no FmrbGfx (the protocol's numbers in
  # the record, as before the boundary).
  VIDEO_ACTIONS = { play: 0, pause: 1, stop: 2, rewind: 3 }.freeze
  def self.video_control(a) = (Regress::Rec.add("video_control #{VIDEO_ACTIONS.fetch(a)}"); nil)
  def self.video_status = Regress::Env.video_status

  # Recorded as the flat list the display gets, seven numbers a region.
  def set_composite_regions_raw(list)
    flat = list.flat_map { |r| [r[:src_x], r[:src_y], r[:dst_x], r[:dst_y], r[:w], r[:h], r[:transparent] ? 1 : 0] }
    __rec(:composite_regions, flat)
    self
  end
  def set_viewport(*a) = (__rec(:viewport, *a); self)
  def clear_viewport = set_viewport(0, 0, 0, 0)
  def set_sprite_clip(x, y, w, h) = (w > 0 ? __rec(:sprite_clip, x + @__ox, y + @__oy, w, h) : __rec(:sprite_clip, x, y, w, h); self)
  def clear_sprite_clip = set_sprite_clip(0, 0, 0, 0)
  def export_frame(path) = (__rec(:export_frame, path); self)

  # Sprites (the app boundary's functions under SpriteImage / SpriteInstance).
  # The record keeps the names it had before the boundary.
  def create_sprite_image(w, h, tc, use) = (id = __next_id; __rec(:sprite_image, id, w, h, tc, use ? 1 : 0); id)
  def delete_sprite_image(id) = (__rec(:delete_sprite_image, id); self)
  def load_sprite_image(id, path) = (__rec(:load_bmp, id, path); self)
  # The pixels as a short digest, so a record stays one line.
  def write_sprite_image(id, x, y, w, h, px) = (__rec(:sprite_pixels, id, x, y, w, h, px.to_s.bytesize, px.to_s.sum); self)
  def set_sprite_image_target(id) = (@__image = id != 0; __rec(:sprite_target, id); self)
  def create_sprite_instance(imgs, x, y, z) = (id = __next_id; __rec(:sprite, id, imgs, x + @__ox, y + @__oy, z); id)
  def delete_sprite_instance(id) = (__rec(:delete_sprite, id); self)
  def move_sprite(id, x, y) = (__rec(:sprite_move, id, x + @__ox, y + @__oy); self)
  def set_sprite_visible(id, v) = (__rec(:sprite_visible, id, v); self)
  def set_sprite_frame(id, i) = (__rec(:sprite_frame, id, i); self)
  def delete_all_sprites = (__rec(:delete_all_sprites); self)
end

# A drawing block (gfx_block.rb compiles it into a program on the display
# side): here the block draws on the canvas. The first draw (and one with
# other values) is recorded in full; a draw with the same values as the
# last is one line, as it is one command to the display.
class GfxBlock
  @@n = 0

  class Error < StandardError; end
  class StructureError < Error; end
  class TooManyRegsError < Error; end
  class UnsupportedKwargError < Error; end
  class PayloadTooLargeError < Error; end

  def initialize(gfx, **initial_values, &block)
    @gfx = gfx
    @values = initial_values
    @block = block
    @destroyed = false
    @id = (@@n += 1)
    @drawn = nil
  end

  def draw(**kwargs)
    @values = @values.merge(kwargs)
    if @drawn == @values
      Regress::Rec.add("gfxblock #{@id} again")
    else
      Regress::Rec.add("gfxblock #{@id} #{Regress::Fmt.v(@values)}")
      @block.call(@gfx, **@values)
      @drawn = @values.dup
    end
    nil
  end

  def destroy = (@destroyed = true; nil)
  def destroyed? = @destroyed
end

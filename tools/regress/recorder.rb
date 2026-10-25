# frozen_string_literal: true
#
# recorder.rb - FmrbGfx's C half, as a recorder.
#
# The Ruby half (lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb: draw_text,
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
  end

  def __rec(name, *a) = Regress::Rec.add("#{@__prefix}#{name} #{Regress::Fmt.args(a)}".rstrip)

  def __font_tag
    f = @__font.length > 1 ? "#{@__font[0]}#{@__font[1]}" : @__font[0].to_s
    "#{f}x#{@__size}"
  end

  def _set_font(family, size = nil) = (@__font = size ? [family, size] : [family])
  def _set_text_size(n) = (@__size = n)

  # An empty text draws nothing (and is not recorded).
  def _draw_text(x, y, s, c, bg = nil)
    return if s.to_s.empty?
    t = "#{@__prefix}text #{Regress::Fmt.v(x)} #{Regress::Fmt.v(y)} #{s.to_s.inspect} #{Regress::Fmt.c(c)}"
    t += " bg=#{Regress::Fmt.c(bg)}" if bg
    Regress::Rec.add("#{t} #{__font_tag}")
  end

  # Font0 for ASCII and misaki 8 for the rest, whatever the font.
  def _draw_text_hybrid(x, y, s, c, bg = nil)
    return if s.to_s.empty?
    t = "#{@__prefix}textm #{Regress::Fmt.v(x)} #{Regress::Fmt.v(y)} #{s.to_s.inspect} #{Regress::Fmt.c(c)}"
    t += " bg=#{Regress::Fmt.c(bg)}" if bg
    Regress::Rec.add("#{t} x#{@__size}")
  end

  # Shapes: the coordinates, then the colour last.
  %i[set_pixel draw_line draw_rect fill_rect draw_circle fill_circle draw_round_rect fill_round_rect
     draw_ellipse fill_ellipse draw_triangle fill_triangle draw_arc fill_arc].each do |m|
    define_method(m) do |*a|
      Regress::Rec.add("#{@__prefix}#{m} #{Regress::Fmt.args(a[0..-2])} #{Regress::Fmt.c(a[-1])}")
      self
    end
  end

  def blend_rect(*a) = (__rec(:blend_rect, *a); self)
  def clear(c = 0) = (Regress::Rec.add("#{@__prefix}clear #{Regress::Fmt.c(c)}"); self)
  def present = (Regress::Rec.present(@__prefix); self)
  def destroy = (__rec(:destroy); nil)
  def draw_wallclock(*a) = (__rec(:draw_wallclock, *a); self)
  def draw_free_iram(*a) = (__rec(:draw_free_iram, *a); self)
  def set_output_level(*) = self
  def set_chroma_level(*) = self
  def _get_pixel(_x, _y) = 0

  def _sync_file(_path, _dest) = true
  def _transfer_file(_path, _dest) = true

  def _file_status(path)
    host = Regress::FakeFS.read_path(path)
    host ? { exists: true, size: File.__regress_size(host) } : { exists: false, size: 0 }
  end

  def __next_id = (Regress::Env.ids += 1)

  def _create_image_from_file(path)
    w, h = Regress::Env.image_size(path)
    return nil unless w
    id = __next_id
    __rec(:create_image, id, path)
    { id: id, width: w, height: h }
  end

  def _draw_image(id, x, y, sx, sy) = (__rec(:draw_image, id, x, y, sx, sy); self)
  def _delete_image(id) = (__rec(:delete_image, id); self)
  def _create_mask(w, h, _data) = (id = __next_id; __rec(:create_mask, id, w, h); id)
  def _delete_mask(id) = (__rec(:delete_mask, id); nil)
  def _draw_image_masked(img, mask, x, y) = (__rec(:draw_image_masked, img, mask, x, y); self)
  def _draw_tile(id, sx, sy, w, h, dx, dy) = (__rec(:draw_tile, id, sx, sy, w, h, dx, dy); self)
  def _video_open(path, x, y, fps, lp) = (__rec(:video_open, path, x, y, fps, lp); Regress::Env.video_info)
  def _video_control(a) = (__rec(:video_control, a); nil)
  def _video_status = Regress::Env.video_status

  def _set_composite_regions(flat) = (__rec(:composite_regions, flat); self)
  def _set_canvas_viewport(*a) = (__rec(:viewport, *a); self)
  def _set_sprite_clip(*a) = (__rec(:sprite_clip, *a); self)
  def _export_frame(path) = (__rec(:export_frame, path); true)

  def _create_sprite_image(w, h, tc, use) = (id = __next_id; __rec(:sprite_image, id, w, h, tc, use); id)
  def _delete_sprite_image(id) = (__rec(:delete_sprite_image, id); nil)
  def _load_sprite_image_bmp(id, path) = (__rec(:load_bmp, id, path); true)
  def _set_sprite_image_target(id) = (__rec(:sprite_target, id); nil)
  def _create_sprite_instance(imgs, x, y, z) = (id = __next_id; __rec(:sprite, id, imgs, x, y, z); id)
  def _delete_sprite_instance(id) = (__rec(:delete_sprite, id); nil)
  def _sprite_move(id, x, y) = (__rec(:sprite_move, id, x, y); nil)
  def _sprite_visible(id, v) = (__rec(:sprite_visible, id, v); nil)
  def _sprite_frame(id, i) = (__rec(:sprite_frame, id, i); nil)
  def _delete_all_sprites = (__rec(:delete_all_sprites); nil)
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

# Spinel base layer for application VMs (system_desktop and, later, other
# Spinel apps).
#
# The mruby app framework gets FmrbApp / FmrbGfx / GfxBlock / Log / Machine from
# C extensions and mrblib (picoruby-fmrb-app). A Spinel-compiled app has no such
# layer, so this file re-implements the pieces the app framework needs, on top of
# the FmrbSpxApp / FmrbSpxGfx FFI shims (fmrb_app_ffi.rb / main/app/fmrb_spx_*.c).
# The dual-build seam is confined here; system_desktop.app.rb and its mixins sit
# on top unchanged.
#
# Spliced before this file in the combined build (T4-3):
#   fmrb_ffi.rb (FmrbSpx: reused for Log / board_millis),
#   fmrb_app_ffi.rb (FmrbSpxApp / FmrbSpxGfx),
#   fmrb_const_generated.rb (FmrbConst), msgpack_pure.rb (MessagePack)
#
# All the language features used here (keyword args, block.call with kwargs,
# Enumerable#partition, Math.sqrt, splat def, String#bytes, setbyte, ::Const)
# were verified against the Spinel compiler before authoring.

module Log
  def self.debug(msg); FmrbSpx.fmrb_spx_log_write(0, msg, msg.bytesize); end
  def self.info(msg);  FmrbSpx.fmrb_spx_log_write(1, msg, msg.bytesize); end
  def self.warn(msg);  FmrbSpx.fmrb_spx_log_write(2, msg, msg.bytesize); end
  def self.error(msg); FmrbSpx.fmrb_spx_log_write(3, msg, msg.bytesize); end
end

module Machine
  def self.board_millis
    FmrbSpx.fmrb_spx_board_millis
  end

  # Coarse busy wait against board_millis (used by the desktop boot animation
  # for frame pacing). Each Spinel app is its own preemptive FreeRTOS task, so
  # this only spins the app's own task; a vTaskDelay FFI would be lighter and is
  # a future improvement (see phase4 report).
  def self.delay_ms(ms)
    target = board_millis + ms
    while board_millis < target
      # spin
    end
    nil
  end
end

# Little-endian readers/writers and NUL-padded name fields over byte Strings.
# NOTE: never define a helper named `name` here -- Spinel resolves e.g.
# `SpxBytes.name` to the built-in Module#name, silently corrupting parsing
# (Phase 2 GUI bug). Use distinct verbs (read_name).
module SpxBytes
  def self.u16(s, off)
    s.getbyte(off) | (s.getbyte(off + 1) << 8)
  end

  def self.u32(s, off)
    s.getbyte(off) | (s.getbyte(off + 1) << 8) |
      (s.getbyte(off + 2) << 16) | (s.getbyte(off + 3) << 24)
  end

  def self.i16(s, off)
    v = u16(s, off)
    v >= 0x8000 ? v - 0x10000 : v
  end

  def self.i32(s, off)
    v = u32(s, off)
    v >= 0x80000000 ? v - 0x100000000 : v
  end

  # Copy `len` bytes from offset into a fresh String (Spinel has no sp_str_chr,
  # so build with setbyte rather than Integer#chr).
  def self.slice(s, off, len)
    out = "\x00" * len
    i = 0
    while i < len
      out.setbyte(i, s.getbyte(off + i))
      i += 1
    end
    out
  end

  # NUL-padded fixed-width field -> String (stops at first NUL).
  def self.read_name(s, off, width)
    len = 0
    while len < width && s.getbyte(off + len) != 0
      len += 1
    end
    slice(s, off, len)
  end

  def self.set_u16(s, off, v)
    s.setbyte(off, v & 0xFF)
    s.setbyte(off + 1, (v >> 8) & 0xFF)
  end

  def self.set_i16(s, off, v)
    set_u16(s, off, v & 0xFFFF)
  end
end

# Graphics API: FFI reimplementation of gfx.c (C methods) + fmrb-gfx.rb (Ruby
# wrapper). The canvas id is a plain instance variable (no boxed mrb_gfx_data).
class FmrbGfx
  # Color constants (RGB332). Both the gfx.c names (WHITE...) and the
  # fmrb-gfx.rb names (COLOR_WHITE...) are provided so callers of either work.
  BLACK   = 0x00
  WHITE   = 0xFF
  RED     = 0xE0
  GREEN   = 0x1C
  BLUE    = 0x03
  YELLOW  = 0xFC
  CYAN    = 0x1F
  MAGENTA = 0xE3
  GRAY    = 0x6D
  COLOR_BLACK   = 0x00
  COLOR_WHITE   = 0xFF
  COLOR_RED     = 0xE0
  COLOR_GREEN   = 0x1C
  COLOR_BLUE    = 0x03
  COLOR_YELLOW  = 0xFC
  COLOR_CYAN    = 0x1F
  COLOR_MAGENTA = 0xE3
  COLOR_GRAY    = 0x6D

  BLEND_ADD = 0
  BLEND_XOR = 1

  FONT_METRICS = {
    [:default] => { char_w: 6,  line_h: 8  },
    [:ja, 8]   => { char_w: 8,  line_h: 8  },
    [:ja, 12]  => { char_w: 12, line_h: 12 },
  }

  attr_reader :current_font, :current_text_size, :canvas_width, :canvas_height, :canvas_id

  def initialize(canvas_id, width: 0, height: 0)
    @canvas_id = canvas_id
    @canvas_width = width
    @canvas_height = height
    @current_font = [:default]
    @current_text_size = 1
  end

  # ---- primitives (forward to the gfx shim with @canvas_id) ----
  def clear(color); FmrbSpxGfx.fmrb_spx_gfx_clear(@canvas_id, color); self; end
  def set_pixel(x, y, color); FmrbSpxGfx.fmrb_spx_gfx_set_pixel(@canvas_id, x, y, color); self; end
  def draw_line(x1, y1, x2, y2, color); FmrbSpxGfx.fmrb_spx_gfx_draw_line(@canvas_id, x1, y1, x2, y2, color); self; end
  def draw_rect(x, y, w, h, color); FmrbSpxGfx.fmrb_spx_gfx_draw_rect(@canvas_id, x, y, w, h, color); self; end
  def fill_rect(x, y, w, h, color); FmrbSpxGfx.fmrb_spx_gfx_fill_rect(@canvas_id, x, y, w, h, color); self; end
  def blend_rect(x, y, w, h, color, mode); FmrbSpxGfx.fmrb_spx_gfx_blend_rect(@canvas_id, x, y, w, h, color, mode); self; end
  def draw_circle(x, y, r, color); FmrbSpxGfx.fmrb_spx_gfx_draw_circle(@canvas_id, x, y, r, color); self; end
  def fill_circle(x, y, r, color); FmrbSpxGfx.fmrb_spx_gfx_fill_circle(@canvas_id, x, y, r, color); self; end
  def draw_round_rect(x, y, w, h, r, color); FmrbSpxGfx.fmrb_spx_gfx_draw_round_rect(@canvas_id, x, y, w, h, r, color); self; end
  def fill_round_rect(x, y, w, h, r, color); FmrbSpxGfx.fmrb_spx_gfx_fill_round_rect(@canvas_id, x, y, w, h, r, color); self; end
  def draw_ellipse(x, y, rx, ry, color); FmrbSpxGfx.fmrb_spx_gfx_draw_ellipse(@canvas_id, x, y, rx, ry, color); self; end
  def fill_ellipse(x, y, rx, ry, color); FmrbSpxGfx.fmrb_spx_gfx_fill_ellipse(@canvas_id, x, y, rx, ry, color); self; end
  def draw_triangle(x0, y0, x1, y1, x2, y2, color); FmrbSpxGfx.fmrb_spx_gfx_draw_triangle(@canvas_id, x0, y0, x1, y1, x2, y2, color); self; end
  def fill_triangle(x0, y0, x1, y1, x2, y2, color); FmrbSpxGfx.fmrb_spx_gfx_fill_triangle(@canvas_id, x0, y0, x1, y1, x2, y2, color); self; end
  def draw_arc(x, y, r0, r1, a0, a1, color); FmrbSpxGfx.fmrb_spx_gfx_draw_arc(@canvas_id, x, y, r0, r1, a0, a1, color); self; end
  def fill_arc(x, y, r0, r1, a0, a1, color); FmrbSpxGfx.fmrb_spx_gfx_fill_arc(@canvas_id, x, y, r0, r1, a0, a1, color); self; end

  # ---- text (with font/size caching, like fmrb-gfx.rb) ----
  def set_font(family, size = nil)
    @current_font = size ? [family, size] : [family]
    fam = family == :ja ? 1 : 0
    sz = size || 12
    FmrbSpxGfx.fmrb_spx_gfx_set_font(@canvas_id, fam, sz)
    self
  end

  def set_text_size(size)
    @current_text_size = size
    FmrbSpxGfx.fmrb_spx_gfx_set_text_size(@canvas_id, size)
    self
  end

  def draw_text(x, y, str, color, bg_color = nil, mixed: false)
    s = str.to_s
    flags = 0
    flags |= 1 if bg_color
    flags |= 2 if mixed
    bg = bg_color || 0
    FmrbSpxGfx.fmrb_spx_gfx_draw_text(@canvas_id, x, y, s, s.bytesize, color, bg, flags)
    self
  end

  # Rendered pixel width of a string with the given (or current) font.
  def text_width(str, family = nil, size = nil)
    key = font_key(family, size)
    metrics = FONT_METRICS[key] || FONT_METRICS[[:default]]
    char_w = metrics[:char_w]
    bytes = str.bytes
    width = 0
    i = 0
    while i < bytes.length
      b = bytes[i]
      if b < 0x80
        width += char_w
        i += 1
      elsif b < 0xC0
        i += 1
      else
        seq_len = if b < 0xE0 then 2
                  elsif b < 0xF0 then 3
                  else 4
                  end
        width += (key == [:default] ? 8 : char_w)
        i += seq_len
      end
    end
    width * @current_text_size
  end

  def font_height(family = nil, size = nil)
    key = font_key(family, size)
    metrics = FONT_METRICS[key] || FONT_METRICS[[:default]]
    metrics[:line_h] * @current_text_size
  end

  def get_pixel(x, y)
    v = FmrbSpxGfx.fmrb_spx_gfx_get_pixel(@canvas_id, x, y)
    v < 0 ? 0 : v
  end

  # ---- present ----
  def present(x = nil, y = nil)
    if x && y
      FmrbSpxGfx.fmrb_spx_gfx_present(@canvas_id, x, y, 1)
    else
      FmrbSpxGfx.fmrb_spx_gfx_present(@canvas_id, 0, 0, 0)
    end
    self
  end

  # ---- CVBS/NTSC output ----
  def set_output_level(level); FmrbSpxGfx.fmrb_spx_gfx_set_output_level(@canvas_id, level); self; end
  def set_chroma_level(level); FmrbSpxGfx.fmrb_spx_gfx_set_chroma_level(@canvas_id, level); self; end

  # ---- masks ----
  def create_mask(width, height, data)
    d = data.to_s
    FmrbSpxGfx.fmrb_spx_gfx_create_mask(@canvas_id, width, height, d, d.bytesize)
  end

  def delete_mask(mask_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_mask(@canvas_id, mask_id)
    self
  end

  def draw_image_masked(image_id, mask_id, x:, y:)
    FmrbSpxGfx.fmrb_spx_gfx_draw_image_masked(@canvas_id, image_id, mask_id, x, y)
    self
  end

  def draw_tile(image_id, src_x, src_y, w, h, dst_x:, dst_y:)
    FmrbSpxGfx.fmrb_spx_gfx_draw_tile(@canvas_id, image_id, src_x, src_y, w, h, dst_x, dst_y)
    self
  end

  # Thick line (no native primitive; stack parallel 1px lines).
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
      fill_rect(x0 - t / 2, y0 - t / 2, t, t, color)
      return self
    end
    len = Math.sqrt(len_sq)
    nx = -dy.to_f / len
    ny = dx.to_f / len
    half = (t - 1) / 2
    k = -half
    while k <= (t - 1 - half)
      ox = (nx * k).round
      oy = (ny * k).round
      draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color)
      k += 1
    end
    self
  end

  # ---- files / images ----
  def transfer_file(path, dest: nil)
    s = path.to_s
    d = (dest || path).to_s
    FmrbSpxGfx.fmrb_spx_gfx_transfer_file(s, s.bytesize, d, d.bytesize) == 1
  end

  def file_status(path)
    s = path.to_s
    sz = FmrbSpxGfx.fmrb_spx_gfx_file_status(s, s.bytesize)
    sz < 0 ? { exists: false, size: 0 } : { exists: true, size: sz }
  end

  def create_image(path)
    s = path.to_s
    buf = FmrbSpxGfx.fmrb_spx_gfx_create_image_from_file(@canvas_id, s, s.bytesize)
    return nil if buf.bytesize == 0
    { id: SpxBytes.u16(buf, 0), width: SpxBytes.u16(buf, 2), height: SpxBytes.u16(buf, 4) }
  end

  def draw_image(image_id, x: 0, y: 0, scale_x: 1.0, scale_y: 0.0)
    # scale_y_fp8 == 0 is interpreted by the backend as "use scale_x"
    # (uniform), matching gfx.c which just multiplies.
    sxfp = (scale_x * 256.0).to_i
    syfp = (scale_y * 256.0).to_i
    FmrbSpxGfx.fmrb_spx_gfx_draw_image(@canvas_id, image_id, x, y, sxfp, syfp)
    self
  end

  def delete_image(image_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_image(@canvas_id, image_id)
    self
  end

  def load_image(path, coord: nil, mode: nil)
    status = file_status(path)
    unless status[:exists]
      transfer_file(path)
    end
    img = create_image(path)
    return if img.nil?
    img_id = img[:id]
    img_w = img[:width]
    img_h = img[:height]
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

  # ---- composite regions / viewport ----
  def set_composite_regions(regions)
    if regions.nil? || regions.empty?
      FmrbSpxGfx.fmrb_spx_gfx_set_composite_regions(@canvas_id, "", 0)
      return self
    end
    count = regions.length
    buf = "\x00" * (count * 14)
    i = 0
    regions.each do |r|
      dst_x = r[:dst_x] || 0
      dst_y = r[:dst_y] || 0
      src_x = r[:src_x] || dst_x
      src_y = r[:src_y] || dst_y
      w = r[:w] || 0
      h = r[:h] || 0
      trans = r[:transparent] ? 1 : 0
      base = i * 14
      SpxBytes.set_i16(buf, base + 0, src_x)
      SpxBytes.set_i16(buf, base + 2, src_y)
      SpxBytes.set_i16(buf, base + 4, dst_x)
      SpxBytes.set_i16(buf, base + 6, dst_y)
      SpxBytes.set_i16(buf, base + 8, w)
      SpxBytes.set_i16(buf, base + 10, h)
      SpxBytes.set_i16(buf, base + 12, trans)
      i += 1
    end
    FmrbSpxGfx.fmrb_spx_gfx_set_composite_regions(@canvas_id, buf, count)
    self
  end

  def set_viewport(src_x, src_y, w, h)
    FmrbSpxGfx.fmrb_spx_gfx_set_canvas_viewport(@canvas_id, src_x, src_y, w, h)
    self
  end

  def clear_viewport
    FmrbSpxGfx.fmrb_spx_gfx_set_canvas_viewport(@canvas_id, 0, 0, 0, 0)
    self
  end

  # ---- sprite low-level (used by SpriteImage / SpriteInstance) ----
  def _create_sprite_image(width, height, trans_color, use_trans)
    FmrbSpxGfx.fmrb_spx_gfx_create_sprite_image(@canvas_id, width, height, trans_color, use_trans)
  end

  def _delete_sprite_image(image_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_sprite_image(@canvas_id, image_id)
    self
  end

  def _load_sprite_image_bmp(image_id, path)
    s = path.to_s
    FmrbSpxGfx.fmrb_spx_gfx_load_sprite_image_bmp(@canvas_id, image_id, s, s.bytesize)
    self
  end

  def _set_sprite_image_target(image_id)
    FmrbSpxGfx.fmrb_spx_gfx_set_sprite_image_target(@canvas_id, image_id)
    self
  end

  def _create_sprite_instance(image_ids, x, y, z_order)
    n = image_ids.length
    buf = "\x00" * (n * 2)
    i = 0
    image_ids.each do |id|
      SpxBytes.set_u16(buf, i * 2, id)
      i += 1
    end
    FmrbSpxGfx.fmrb_spx_gfx_create_sprite_instance(@canvas_id, buf, n, x, y, z_order)
  end

  def _delete_sprite_instance(instance_id)
    FmrbSpxGfx.fmrb_spx_gfx_delete_sprite_instance(@canvas_id, instance_id)
    self
  end

  def _sprite_move(instance_id, x, y)
    FmrbSpxGfx.fmrb_spx_gfx_sprite_move(@canvas_id, instance_id, x, y)
    self
  end

  def _sprite_visible(instance_id, visible)
    FmrbSpxGfx.fmrb_spx_gfx_sprite_visible(@canvas_id, instance_id, visible ? 1 : 0)
    self
  end

  def _sprite_frame(instance_id, frame_index)
    FmrbSpxGfx.fmrb_spx_gfx_sprite_frame(@canvas_id, instance_id, frame_index)
    self
  end

  def _delete_all_sprites
    FmrbSpxGfx.fmrb_spx_gfx_delete_all_sprites(@canvas_id)
    self
  end

  # No-op on Spinel: there is no boxed C resource to release (the canvas is
  # owned by the app context and freed in _cleanup).
  def destroy
    nil
  end

  private

  def font_key(family, size)
    return @current_font if family.nil?
    family == :default ? [:default] : [family, (size || 8)]
  end
end

# GfxBlock: immediate-mode port. The mruby version compiles the drawing block to
# a WROVER program and re-sends only changed registers; the Spinel port simply
# replays the block against the real gfx on every draw. Functionally identical
# (same pixels), without the define-program shim. Slower (re-sends all ops), but
# correct; batching can be added later if profiling shows it matters.
class GfxBlock
  # Recorder that forwards the block's DSL calls straight to the gfx instance.
  # Provides the same method names + aliases the mruby Recorder exposes.
  class Recorder
    def initialize(gfx); @g = gfx; end
    def clear(color); @g.clear(color); end
    def fill_rect(x, y, w, h, color); @g.fill_rect(x, y, w, h, color); end
    def draw_rect(x, y, w, h, color); @g.draw_rect(x, y, w, h, color); end
    alias rect draw_rect
    def fill_round_rect(x, y, w, h, r, color); @g.fill_round_rect(x, y, w, h, r, color); end
    def draw_round_rect(x, y, w, h, r, color); @g.draw_round_rect(x, y, w, h, r, color); end
    def draw_line(x0, y0, x1, y1, color); @g.draw_line(x0, y0, x1, y1, color); end
    alias line draw_line
    def fill_circle(x, y, r, color); @g.fill_circle(x, y, r, color); end
    def draw_text(x, y, str, color); @g.draw_text(x, y, str, color); end
    alias text draw_text
  end

  def initialize(gfx, **initial_values, &block)
    @gfx = gfx
    @block = block
    @rec = Recorder.new(gfx)
    @destroyed = false
    # Initial draw with the sample values (mruby issues an initial EXEC too).
    block.call(@rec, **initial_values)
  end

  def draw(**kwargs)
    return if @destroyed
    @block.call(@rec, **kwargs)
    nil
  end

  def destroy
    @destroyed = true
  end

  def destroyed?
    @destroyed
  end
end

# Application framework: FFI reimplementation of app.c (C methods) + fmrb-app.rb
# (Ruby framework). Subclasses override on_create / on_update / on_event / etc.
class FmrbApp
  TITLE_BAR_H = 11
  CORNER_R = 4
  TRANSPARENT_COLOR = 0x01
  SCROLLBAR_W = 10
  SCROLLBAR_BTN_H = 10

  attr_reader :name, :running, :window_width, :window_height, :pos_x, :pos_y, :platform, :fullscreen, :rounded_corners

  CLOSE_BTN_CX_OFFSET = 6
  CLOSE_BTN_CY        = 5
  CLOSE_BTN_R         = 3
  CLOSE_BTN_HIT_R     = 5
  CLOSE_BTN_NORMAL_COLOR  = 0xFF
  CLOSE_BTN_PRESSED_COLOR = 0x49

  def initialize
    Log.debug("initialize")
    @running = false
    @close_btn_pressed = false
    @_timers = []

    buf = FmrbSpxApp.fmrb_spx_app_init   # 50-byte snapshot; creates canvas(es)
    @name = SpxBytes.read_name(buf, 0, 32)
    @fullscreen = buf.getbyte(32) != 0
    @rounded_corners = buf.getbyte(33) != 0
    @platform = buf.getbyte(34) == 1 ? :esp32 : :linux
    @window_width = SpxBytes.u16(buf, 36)
    @window_height = SpxBytes.u16(buf, 38)
    @pos_x = SpxBytes.u16(buf, 40)
    @pos_y = SpxBytes.u16(buf, 42)
    @canvas = buf.getbyte(44) != 0 ? SpxBytes.u16(buf, 46) : nil
    @bg_canvas = buf.getbyte(45) != 0 ? SpxBytes.u16(buf, 48) : nil
    Log.debug("name=#{@name}")

    if @canvas
      @gfx = FmrbGfx.new(@canvas, width: @window_width, height: @window_height)
      if @fullscreen
        @user_area_x0 = 0
        @user_area_y0 = 0
        @user_area_x1 = @window_width
        @user_area_y1 = @window_height
        @user_area_width = @window_width
        @user_area_height = @window_height
      else
        @user_area_x0 = 1
        @user_area_y0 = TITLE_BAR_H
        @user_area_x1 = @window_width - 1
        @user_area_y1 = @window_height - 1
        @user_area_width = @window_width - 2
        @user_area_height = @window_height - TITLE_BAR_H - 1
      end

      if @bg_canvas
        @bg_gfx = FmrbGfx.new(@bg_canvas, width: @window_width, height: @window_height)
      else
        @bg_gfx = nil
      end

      unless @fullscreen
        draw_window_frame
      end
    else
      @gfx = nil
      @bg_gfx = nil
      Log.debug("Headless app: no graphics initialized")
    end
  end

  def draw_window_frame
    return if @fullscreen
    return unless @gfx
    saved_font = @gfx.current_font
    saved_size = @gfx.current_text_size
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    _paint_frame
    if saved_font != [:default]
      if saved_font.length == 2
        @gfx.set_font(saved_font[0], saved_font[1])
      else
        @gfx.set_font(saved_font[0])
      end
    end
    @gfx.set_text_size(saved_size) unless saved_size == 1
    _apply_rounded_corner_regions
  end

  def clear_user_area(color = FmrbGfx::BLACK)
    return unless @gfx
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, color)
  end

  private

  # Draw the window frame directly (immediate mode). The mruby build used a
  # cached GfxBlock program here, but Spinel cannot store a proc that captures
  # outer locals ("later slice"), so the base class draws frame/scrollbar
  # inline instead. Functionally identical; re-issues all ops each redraw.
  def _paint_frame
    return unless @gfx
    g = @gfx
    w = @window_width
    h = @window_height
    # Title bar with rounded top corners.
    g.fill_round_rect(0, 0, w, TITLE_BAR_H, CORNER_R, 0xC5)
    g.fill_rect(0, CORNER_R, w, TITLE_BAR_H - CORNER_R, 0xC5)
    # Menu button (hamburger) + title text.
    g.fill_rect(3, 3, 9, 1, 0xFB)
    g.fill_rect(3, 5, 9, 1, 0xFB)
    g.fill_rect(3, 7, 9, 1, 0xFB)
    g.draw_text(15, 2, @name, FmrbGfx::WHITE)
    # Close button + rounded border.
    g.fill_circle(w - 6, 5, 3, 0xFF)
    g.draw_round_rect(0, 0, w, h, CORNER_R, 0x60)
    # Re-stamp the outer corner pixels with the canvas color key so the rounded
    # corners composite as transparent again after clears/resizes.
    t = TRANSPARENT_COLOR
    g.draw_line(0, 0, 1, 0, t)
    g.draw_line(0, 1, 0, 1, t)
    g.draw_line(w - 2, 0, w - 1, 0, t)
    g.draw_line(w - 1, 1, w - 1, 1, t)
    g.draw_line(0, h - 2, 0, h - 1, t)
    g.draw_line(1, h - 1, 1, h - 1, t)
    g.draw_line(w - 1, h - 2, w - 1, h - 1, t)
    g.draw_line(w - 2, h - 1, w - 2, h - 1, t)
  end

  def _apply_rounded_corner_regions
    return if @fullscreen
    return unless @gfx
    return unless @rounded_corners
    return if @bg_canvas
    w = @window_width
    h = @window_height
    return if @composite_region_w == w && @composite_region_h == h
    c = CORNER_R
    @gfx.set_composite_regions([
      { dst_x: 0,     dst_y: 0,     w: c,         h: c,         transparent: true  },
      { dst_x: w - c, dst_y: 0,     w: c,         h: c,         transparent: true  },
      { dst_x: 0,     dst_y: h - c, w: c,         h: c,         transparent: true  },
      { dst_x: w - c, dst_y: h - c, w: c,         h: c,         transparent: true  },
      { dst_x: c,     dst_y: 0,     w: w - 2 * c, h: c,         transparent: false },
      { dst_x: c,     dst_y: h - c, w: w - 2 * c, h: c,         transparent: false },
      { dst_x: 0,     dst_y: c,     w: w,         h: h - 2 * c, transparent: false },
    ])
    @composite_region_w = w
    @composite_region_h = h
  end

  public

  def draw_scrollbar(scroll, total, visible, x = @user_area_x0, y = @user_area_y0, w = @user_area_width, h = @user_area_height)
    return if total <= visible
    return unless @gfx
    btn_h = SCROLLBAR_BTN_H
    track_y = y + btn_h
    track_h = h - btn_h * 2
    return if track_h <= 4

    thumb_h = [track_h * visible / total, 6].max
    max_scroll = total - visible
    thumb_y = track_y + (max_scroll > 0 ? (track_h - thumb_h) * scroll / max_scroll : 0)

    # Immediate draw (see _paint_frame note on why the base class avoids
    # stored GfxBlock programs under Spinel).
    g = @gfx
    bar_x = x + w - SCROLLBAR_W
    border = FmrbConst::THEME_BORDER
    bg = FmrbConst::THEME_WINDOW_BG
    cx = bar_x + SCROLLBAR_W / 2
    dy = y + h - btn_h
    g.draw_line(bar_x, y, bar_x, y + h - 1, border)
    g.fill_rect(bar_x, y, SCROLLBAR_W, btn_h, bg)
    g.draw_rect(bar_x, y, SCROLLBAR_W, btn_h, border)
    g.draw_line(cx, y + 2, cx - 3, y + 7, border)
    g.draw_line(cx, y + 2, cx + 3, y + 7, border)
    g.draw_line(cx - 3, y + 7, cx + 3, y + 7, border)
    g.fill_rect(bar_x, dy, SCROLLBAR_W, btn_h, bg)
    g.draw_rect(bar_x, dy, SCROLLBAR_W, btn_h, border)
    g.draw_line(cx, dy + 7, cx - 3, dy + 2, border)
    g.draw_line(cx, dy + 7, cx + 3, dy + 2, border)
    g.draw_line(cx - 3, dy + 2, cx + 3, dy + 2, border)
    g.fill_rect(bar_x + 2, thumb_y, SCROLLBAR_W - 3, thumb_h, border)
    self
  end

  def scrollbar_hit(click_x, click_y, x = @user_area_x0, y = @user_area_y0, w = @user_area_width, h = @user_area_height)
    bar_x = x + w - SCROLLBAR_W - 1
    return nil unless click_x >= bar_x && click_y >= y && click_y < y + h
    btn_h = SCROLLBAR_BTN_H
    mid_y = y + h / 2
    if click_y < y + btn_h
      :up
    elsif click_y >= y + h - btn_h
      :down
    elsif click_y < mid_y
      :up
    else
      :down
    end
  end

  # ---- modifier key helpers ----
  def ev_ctrl?(ev);  ((ev[:modifier] || 0) & 0x0C) != 0; end
  def ev_shift?(ev); ((ev[:modifier] || 0) & 0x03) != 0; end
  def ev_alt?(ev);   ((ev[:modifier] || 0) & 0x30) != 0; end

  # ---- lifecycle methods (override in subclass) ----
  def on_create; Log.debug("on_create"); end
  def on_update; 330; end
  def on_destroy; Log.debug("on_destroy"); end
  def on_suspend; Log.debug("on_suspend"); end
  def on_resume; Log.debug("on_resume"); end
  def on_resize(new_width, new_height); end
  # Default no-op so the Spinel build can statically resolve the call in
  # _dispatch_control (mruby relied on respond_to? + dynamic dispatch; Spinel
  # cannot compile a call to a method that is defined nowhere). Subclasses
  # override to handle custom APP_CONTROL commands.
  def on_control(msg); nil; end

  def on_event(ev)
    if ev[:button] == 1 && (ev[:type] == :mouse_down || ev[:type] == :mouse_up)
      cx = @window_width - CLOSE_BTN_CX_OFFSET
      cy = CLOSE_BTN_CY
      hit = (ev[:x] - cx).abs <= CLOSE_BTN_HIT_R && (ev[:y] - cy).abs <= CLOSE_BTN_HIT_R
      case ev[:type]
      when :mouse_down
        if hit && !@fullscreen && @gfx
          @close_btn_pressed = true
          @gfx.fill_circle(cx, cy, CLOSE_BTN_R, CLOSE_BTN_PRESSED_COLOR)
          @gfx.present
        end
      when :mouse_up
        if @close_btn_pressed
          @close_btn_pressed = false
          if hit
            stop
          elsif @gfx
            @gfx.fill_circle(cx, cy, CLOSE_BTN_R, CLOSE_BTN_NORMAL_COLOR)
            @gfx.present
          end
        elsif hit
          stop
        end
      end
    end
    if ev[:type] == :mouse_up && ev[:button] == 3 && ev[:y] < 11
      request_reload if _is_file_app
    end
  end

  def request_reload
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "reload_confirm" })
  end

  # ---- main loop (poll form; C _spin's dispatch is reimplemented below) ----
  def main_loop
    Log.debug("main_loop started")
    @suspended = false
    loop do
      return if !@running
      if @suspended
        _spin(500)
        next
      end
      timeout_ms = on_update
      _spin(timeout_ms)
    end
  end

  # Poll messages up to timeout_ms and dispatch them, then run due timers.
  # Replaces FmrbApp#_spin (which dispatched via mrb_funcall in C).
  def _spin(timeout_ms)
    target = Machine.board_millis + timeout_ms
    loop do
      now = Machine.board_millis
      break if now >= target
      remaining = target - now
      msg = _poll_message(remaining)
      break if msg.nil?
      _dispatch_message(msg)
    end
    _run_timers
    nil
  end

  # { type:, src_pid:, data: } or nil on timeout.
  def _poll_message(timeout_ms)
    data = FmrbSpxApp.fmrb_spx_app_recv_message(timeout_ms, FmrbSpxApp.type_out, FmrbSpxApp.src_out)
    type = FmrbSpxApp.read_i32(FmrbSpxApp.type_out)
    return nil if type < 0
    src = FmrbSpxApp.read_i32(FmrbSpxApp.src_out)
    { type: type, src_pid: src, data: data }
  end

  def _dispatch_message(msg)
    t = msg[:type]
    if t == FmrbConst::MSG_TYPE_HID_EVENT
      ev = _parse_hid_event(msg[:data])
      on_event(ev) if ev
    elsif t == FmrbConst::MSG_TYPE_APP_CONTROL
      _dispatch_control(msg[:data])
    end
  end

  # Parse a raw HID event payload into the same symbol-keyed Hash the mruby C
  # dispatcher built (fmrb_hid_msg.h layouts). Returns nil for unknown/short.
  def _parse_hid_event(data)
    return nil if data.bytesize < 1
    subtype = data.getbyte(0)
    case subtype
    when 1, 2  # KEY_DOWN / KEY_UP
      return nil if data.bytesize < 5
      { type: (subtype == 1 ? :key_down : :key_up),
        keycode: data.getbyte(1),
        scancode: data.getbyte(2),
        modifier: data.getbyte(3),
        character: data.getbyte(4) }
    when 4, 5  # MOUSE_BUTTON_DOWN / UP
      return nil if data.bytesize < 6
      { type: (subtype == 4 ? :mouse_down : :mouse_up),
        button: data.getbyte(1),
        x: SpxBytes.u16(data, 2),
        y: SpxBytes.u16(data, 4) }
    when 3  # MOUSE_MOVE (kernel sends 6-byte [subtype,button,x_lo,x_hi,y_lo,y_hi])
      return nil if data.bytesize < 6
      { type: :mouse_move,
        x: SpxBytes.u16(data, 2),
        y: SpxBytes.u16(data, 4) }
    when 6, 7  # GAMEPAD_BUTTON_DOWN / UP
      return nil if data.bytesize < 3
      { type: (subtype == 6 ? :gamepad_down : :gamepad_up),
        gamepad_id: data.getbyte(1),
        button: data.getbyte(2) }
    when 8  # GAMEPAD_AXIS
      return nil if data.bytesize < 5
      { type: :gamepad_axis,
        gamepad_id: data.getbyte(1),
        axis: data.getbyte(2),
        value: SpxBytes.i16(data, 3) }
    else
      nil
    end
  end

  def _dispatch_control(data)
    h = MessagePack.unpack(data)
    return unless h.is_a?(Hash)
    cmd = h["cmd"]
    return unless cmd.is_a?(String)
    if cmd == "resize"
      w = h["width"]
      ht = h["height"]
      if w.is_a?(Integer) && ht.is_a?(Integer)
        @window_width = w
        @window_height = ht
        @user_area_width = w - 2
        @user_area_height = ht - 12
        @user_area_x1 = w - 1
        @user_area_y1 = ht - 1
        on_resize(w, ht)
      end
    elsif cmd == "suspend" || cmd == "resume" || cmd == "stop" || cmd == "clear_and_stop"
      _handle_system_control(h)
    else
      on_control(h)
    end
  end

  def _handle_system_control(msg)
    case msg["cmd"]
    when "suspend"
      @suspended = true
      on_suspend
      Log.info("App #{@name} suspended")
    when "resume"
      @suspended = false
      on_resume
      Log.info("App #{@name} resumed")
    when "stop"
      Log.info("App #{@name} received stop command")
      stop
    when "clear_and_stop"
      Log.info("App #{@name} clearing canvas and stopping")
      if @gfx
        @gfx.clear(0x00)
        @gfx.present
      end
      stop
    end
  end

  # ---- timers (Ruby-side; C cannot call a Ruby block) ----
  # NOTE: the mruby FmrbApp#set_timer was a non-functional stub. This gives a
  # working implementation; when unused, @_timers stays empty and _run_timers
  # is a no-op, so no behavior diverges for apps that never call set_timer.
  def set_timer(interval, &blk)
    id = (@_timer_seq ||= 0) + 1
    @_timer_seq = id
    @_timers << { id: id, at: Machine.board_millis + interval, interval: interval, blk: blk }
    id
  end

  def clear_time(timer_id)
    @_timers.reject! { |t| t[:id] == timer_id } if @_timers
    nil
  end

  def _run_timers
    return if @_timers.nil? || @_timers.empty?
    now = Machine.board_millis
    # Explicit split rather than `due, rest = ...partition` -- Spinel cannot
    # multi-assign from a partition on a poly (Hash-element) array.
    due = []
    keep = []
    @_timers.each do |t|
      if t[:at] <= now
        due << t
      else
        keep << t
      end
    end
    @_timers = keep
    due.each do |t|
      blk = t[:blk]
      blk.call if blk
    end
  end

  def subscribe(topic)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "subscribe", "topic" => topic })
  end

  def unsubscribe(topic)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "unsubscribe", "topic" => topic })
  end

  def publish(topic, data = nil)
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "publish", "topic" => topic, "data" => data })
  end

  def request_file_select(mode = "open")
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      { "cmd" => "file_select", "mode" => mode })
  end

  def send_message(dest_pid, msg_type, data)
    binary_data = MessagePack.pack(data)
    _send_message(dest_pid, msg_type, binary_data)
  end

  def set_window_position(x, y)
    _set_window_param(:pos_x, x)
    _set_window_param(:pos_y, y)
    @gfx.present if @gfx
    self
  end

  def create_canvas_gfx(width:, height:, z_offset: 1, transparent: false, transparent_color: 0)
    id = _create_canvas(width, height, z_offset, transparent ? 1 : 0, transparent_color)
    FmrbGfx.new(id, width: width, height: height)
  end

  def delete_canvas_gfx(gfx)
    _delete_canvas(gfx.canvas_id)
  end

  def destroy
    Log.debug("destroy() called")
    begin
      exit_data = MessagePack.pack({ "cmd" => "exit" })
      _send_message(0, FmrbConst::MSG_TYPE_APP_CONTROL, exit_data)
    rescue => e
      Log.error("Failed to send exit notification: #{e}")
    end
    if @gfx
      @gfx.destroy
      @gfx = nil
    end
    on_destroy
    _cleanup
  end

  def start
    Log.debug("start() called")
    @running = true
    on_create
    main_loop
    destroy
  end

  def stop
    @running = false
  end

  # ---- instance FFI wrappers (were C methods in app.c) ----
  def _send_message(dest_pid, msg_type, data)
    s = data.to_s
    FmrbSpxApp.fmrb_spx_app_send_message(dest_pid, msg_type, s, s.bytesize) == 1
  end

  def _set_window_param(param, value)
    which = param == :pos_y ? 1 : 0
    FmrbSpxApp.fmrb_spx_app_set_window_param(which, value)
    self
  end

  def _is_file_app
    FmrbSpxApp.fmrb_spx_app_is_file_app == 1
  end

  def _create_canvas(w, h, z_offset, use_transparent, transparent_color)
    FmrbSpxApp.fmrb_spx_app_create_canvas(w, h, z_offset, use_transparent, transparent_color)
  end

  def _delete_canvas(canvas_id)
    FmrbSpxApp.fmrb_spx_app_delete_canvas(canvas_id)
    nil
  end

  def _cleanup
    FmrbSpxApp.fmrb_spx_app_cleanup
    nil
  end

  # ---- class methods (were C class methods; parse packed :binstr records) ----
  def self.ps
    buf = FmrbSpxApp.fmrb_spx_app_ps
    count = buf.bytesize / 60
    list = []
    i = 0
    while i < count
      b = i * 60
      list << {
        id: buf.getbyte(b + 0),
        state: buf.getbyte(b + 1),
        type: buf.getbyte(b + 2),
        vm_type: buf.getbyte(b + 3),
        gen: SpxBytes.u32(buf, b + 4),
        stack_water: SpxBytes.u32(buf, b + 8),
        mem_total: SpxBytes.u32(buf, b + 12),
        mem_used: SpxBytes.u32(buf, b + 16),
        mem_free: SpxBytes.u32(buf, b + 20),
        mem_frag: SpxBytes.i32(buf, b + 24),
        name: SpxBytes.read_name(buf, b + 28, 32),
      }
      i += 1
    end
    list
  end

  def self.heap_info
    buf = FmrbSpxApp.fmrb_spx_app_heap_info
    {
      free: SpxBytes.u32(buf, 0),
      total: SpxBytes.u32(buf, 4),
      min_free: SpxBytes.u32(buf, 8),
      largest_block: SpxBytes.u32(buf, 12),
      iram_free: SpxBytes.u32(buf, 16),
      iram_total: SpxBytes.u32(buf, 20),
    }
  end

  def self.sys_pool_info
    buf = FmrbSpxApp.fmrb_spx_app_sys_pool_info
    {
      total: SpxBytes.u32(buf, 0),
      used: SpxBytes.u32(buf, 4),
      free: SpxBytes.u32(buf, 8),
      used_blocks: SpxBytes.u32(buf, 12),
      free_blocks: SpxBytes.u32(buf, 16),
    }
  end

  def self.gfx_stats
    buf = FmrbSpxApp.fmrb_spx_app_gfx_stats
    { cmds: SpxBytes.u32(buf, 0), presents: SpxBytes.u32(buf, 4) }
  end

  def self._get_last_error
    buf = FmrbSpxApp.fmrb_spx_app_last_error
    return nil if buf.bytesize == 0
    { name: SpxBytes.read_name(buf, 0, 64), error: SpxBytes.read_name(buf, 64, 112) }
  end

  def self.config(section)
    s = section.to_s
    buf = FmrbSpxApp.fmrb_spx_app_config(s, s.bytesize)
    return nil if buf.bytesize == 0
    off = 0
    table_count = buf.getbyte(off)
    off += 1
    tables = []
    t = 0
    while t < table_count
      kv_count = buf.getbyte(off)
      off += 1
      h = {}
      k = 0
      while k < kv_count
        klen = buf.getbyte(off)
        off += 1
        key = SpxBytes.slice(buf, off, klen)
        off += klen
        vlen = SpxBytes.u16(buf, off)
        off += 2
        val = SpxBytes.slice(buf, off, vlen)
        off += vlen
        h[key] = val
        k += 1
      end
      tables << h
      t += 1
    end
    tables
  end

  def self.wallclock
    buf = FmrbSpxApp.fmrb_spx_app_wallclock
    return nil if buf.bytesize == 0
    _decode_wallclock(buf)
  end

  def self.set_wallclock(year, month, day, hour, minute, second)
    buf = FmrbSpxApp.fmrb_spx_app_set_wallclock(year, month, day, hour, minute, second)
    return nil if buf.bytesize == 0
    _decode_wallclock(buf)
  end

  def self._decode_wallclock(buf)
    {
      year: SpxBytes.u16(buf, 0),
      month: SpxBytes.u16(buf, 2),
      day: SpxBytes.u16(buf, 4),
      hour: SpxBytes.u16(buf, 6),
      minute: SpxBytes.u16(buf, 8),
      second: SpxBytes.u16(buf, 10),
    }
  end

  def self.enable_cursor
    FmrbSpxApp.fmrb_spx_app_enable_cursor
    nil
  end

  def self.set_cursor_visible(visible)
    FmrbSpxApp.fmrb_spx_app_set_cursor_visible(visible ? 1 : 0)
    nil
  end

  def self.reboot
    FmrbSpxApp.fmrb_spx_app_reboot
    nil
  end

  def self.wifi_info
    buf = FmrbSpxApp.fmrb_spx_app_wifi_info
    return nil if buf.bytesize == 0
    {
      connected: buf.getbyte(0) != 0,
      ip: SpxBytes.read_name(buf, 1, 16),
      ssid: SpxBytes.read_name(buf, 17, 33),
      hostname: SpxBytes.read_name(buf, 50, 32),
    }
  end

  def self._clear_cache(path)
    s = path.to_s
    buf = FmrbSpxApp.fmrb_spx_app_clear_cache(s, s.bytesize)
    {
      ok: buf.getbyte(0) != 0,
      deleted: SpxBytes.u32(buf, 1),
      status: SpxBytes.i32(buf, 5),
    }
  end

  def self.usb_devices
    buf = FmrbSpxApp.fmrb_spx_app_usb_devices
    count = buf.bytesize / 10
    list = []
    i = 0
    while i < count
      b = i * 10
      list << {
        type: _usb_type_str(buf.getbyte(b + 0)),
        layout_valid: buf.getbyte(b + 1) != 0,
        vid: SpxBytes.u16(buf, b + 2),
        pid: SpxBytes.u16(buf, b + 4),
        addr: buf.getbyte(b + 6),
        slot: buf.getbyte(b + 7),
        report_len: SpxBytes.u16(buf, b + 8),
      }
      i += 1
    end
    list
  end

  def self._usb_type_str(code)
    case code
    when 1 then "KBD"
    when 2 then "MOUSE"
    when 3 then "GAMEPAD"
    else "OTHER"
    end
  end

  def self.hid_raw_subscribe(slot)
    FmrbSpxApp.fmrb_spx_app_hid_raw_subscribe(slot) == 1
  end

  def self.hid_raw_unsubscribe(slot)
    FmrbSpxApp.fmrb_spx_app_hid_raw_unsubscribe(slot) == 1
  end
end

# Spinel FFI for EditorCore (picoruby-fmrb-editor-core), plus the EditorCore
# module the editor calls.
#
# A separate file from fmrb_app_ffi.rb on purpose: Spinel emits an extern for
# every declared ffi_func, and main/app/fmrb_spx_editor.c is only compiled when
# the EDITOR VM is Spinel. Splicing these into the shared app FFI would break the
# Spinel desktop's link in a build where the editor stays on mruby.
# EditorCore (picoruby-fmrb-editor-core) through main/app/fmrb_spx_editor.c.
# Only the editor program splices this; the desktop never calls it. Strings come
# back as :binstr (pointer + sp_net_bin_len), which is why every text-returning
# entry point takes the window (col0, max_cols) rather than handing out a whole
# line: nothing is copied on either side of the boundary.
module FmrbSpxEc
  ffi_func :fmrb_spx_ec_open_slot,   [], :int
  ffi_func :fmrb_spx_ec_close_slot,  [:int], :void
  ffi_func :fmrb_spx_ec_reset,       [:int], :int
  ffi_func :fmrb_spx_ec_line_count,  [:int], :int
  ffi_func :fmrb_spx_ec_line_length, [:int, :int], :int
  ffi_func :fmrb_spx_ec_doc_bytesize, [:int], :int
  ffi_func :fmrb_spx_ec_mem_used,    [], :int
  ffi_func :fmrb_spx_ec_set_hl,      [:int, :int], :void
  ffi_func :fmrb_spx_ec_render_text, [:int, :int, :int, :int], :binstr
  ffi_func :fmrb_spx_ec_render_hl,   [:int, :int, :int, :int], :binstr
  ffi_func :fmrb_spx_ec_render_width, [:int, :int, :int, :int], :binstr
  ffi_func :fmrb_spx_ec_char_at,     [:int, :int, :int], :binstr
  ffi_func :fmrb_spx_ec_insert_text, [:int, :int, :int, :str, :int], :int
  ffi_func :fmrb_spx_ec_split_line,  [:int, :int, :int], :int
  ffi_func :fmrb_spx_ec_join_line,   [:int, :int], :int
  ffi_func :fmrb_spx_ec_delete_char, [:int, :int, :int], :int
  ffi_func :fmrb_spx_ec_delete_range, [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_ec_insert_multiline, [:int, :int, :int, :str, :int], :binstr
  ffi_func :fmrb_spx_ec_load_file,   [:int, :str], :int
  ffi_func :fmrb_spx_ec_save_file,   [:int, :str], :int
  ffi_func :fmrb_spx_ec_find,        [:int, :str, :int, :int, :int, :int], :binstr
  ffi_func :fmrb_spx_ec_copy_range,  [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_ec_paste_at,    [:int, :int, :int], :binstr
  ffi_func :fmrb_spx_ec_clipboard_length, [:int], :int
end

# EditorCore as the editor sees it. The mruby build gets this module from the C
# gem (module functions on EditorCore); the Spinel build gets this shim, so the
# editor source calls the same names either way.
module EditorCore
  def self.slot
    @slot = FmrbSpxEc.fmrb_spx_ec_open_slot if @slot.nil?
    @slot
  end

  def self.reset;        FmrbSpxEc.fmrb_spx_ec_reset(slot); end
  def self.line_count;   FmrbSpxEc.fmrb_spx_ec_line_count(slot); end
  def self.line_length(y); FmrbSpxEc.fmrb_spx_ec_line_length(slot, y); end
  def self.doc_bytesize; FmrbSpxEc.fmrb_spx_ec_doc_bytesize(slot); end
  def self.mem_used;     FmrbSpxEc.fmrb_spx_ec_mem_used; end
  def self.hl_enabled=(on); FmrbSpxEc.fmrb_spx_ec_set_hl(slot, on ? 1 : 0); end

  def self.render_text(y, col0, n); FmrbSpxEc.fmrb_spx_ec_render_text(slot, y, col0, n); end
  def self.render_hl(y, col0, n);   FmrbSpxEc.fmrb_spx_ec_render_hl(slot, y, col0, n); end
  def self.render_width(y, col0, n); FmrbSpxEc.fmrb_spx_ec_render_width(slot, y, col0, n); end
  def self.char_at(y, x);           FmrbSpxEc.fmrb_spx_ec_char_at(slot, y, x); end

  def self.insert_text(y, x, str)
    FmrbSpxEc.fmrb_spx_ec_insert_text(slot, y, x, str, str.bytesize)
  end

  def self.split_line(y, x);  FmrbSpxEc.fmrb_spx_ec_split_line(slot, y, x); end
  def self.join_line(y);      FmrbSpxEc.fmrb_spx_ec_join_line(slot, y); end
  def self.delete_char(y, x); FmrbSpxEc.fmrb_spx_ec_delete_char(slot, y, x); end

  def self.delete_range(sy, sx, ey, ex)
    FmrbSpxEc.fmrb_spx_ec_delete_range(slot, sy, sx, ey, ex)
  end

  def self.insert_multiline(y, x, str)
    FmrbSpxEc.fmrb_spx_ec_insert_multiline(slot, y, x, str, str.bytesize)
  end

  def self.load_file(path); FmrbSpxEc.fmrb_spx_ec_load_file(slot, path); end
  def self.save_file(path); FmrbSpxEc.fmrb_spx_ec_save_file(slot, path); end

  def self.find(query, from_y, from_x, after)
    FmrbSpxEc.fmrb_spx_ec_find(slot, query, query.bytesize, from_y, from_x, after ? 1 : 0)
  end

  def self.copy_range(sy, sx, ey, ex)
    FmrbSpxEc.fmrb_spx_ec_copy_range(slot, sy, sx, ey, ex)
  end

  def self.paste_at(y, x);      FmrbSpxEc.fmrb_spx_ec_paste_at(slot, y, x); end
  def self.clipboard_length;    FmrbSpxEc.fmrb_spx_ec_clipboard_length(slot); end

  # Readers for the packed records (same layout as the mruby gem's mrblib).
  def self.rec_u32(rec, off)
    (rec.getbyte(off) || 0) |
      ((rec.getbyte(off + 1) || 0) << 8) |
      ((rec.getbyte(off + 2) || 0) << 16) |
      ((rec.getbyte(off + 3) || 0) << 24)
  end

  def self.pos_y(rec); rec_u32(rec, 0); end
  def self.pos_x(rec); rec_u32(rec, 4); end
  def self.found?(rec); (rec.getbyte(0) || 0) != 0; end
  def self.find_y(rec); rec_u32(rec, 1); end
  def self.find_x(rec); rec_u32(rec, 5); end
end

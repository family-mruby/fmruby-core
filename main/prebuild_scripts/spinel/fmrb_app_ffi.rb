# Spinel FFI declarations for the FmrbApp / FmrbGfx shims
# (main/app/fmrb_spx_app.h, main/app/fmrb_spx_gfx.h).
#
# Same conventions as fmrb_ffi.rb (the kernel shim): ffi_* declarations live
# directly in the module body; structured data crosses as fixed-layout byte
# buffers read back with getbyte (:binstr), never boxed into a symbol Hash on a
# hot path (Phase 0 finding). The C side is main/app/fmrb_spx_app.c / _gfx.c.
#
# The Spinel system_desktop combined program splices this file together with
# fmrb_ffi.rb (FmrbSpx, reused for Log / board_millis) before
# fmrb_app_base_spinel.rb.

# The app VM reuses only the two generic (non-kernel) FmrbSpx entry points --
# millis + logging (defined in fmrb_spx_common.c). The full kernel FmrbSpx
# (fmrb_ffi.rb) is deliberately NOT spliced into an app program: Spinel emits an
# extern for every declared ffi_func, so splicing it would drag the whole kernel
# shim (recv_message, windows_snapshot, spawn_app_req, ...) into the app's link,
# breaking a mixed mruby-kernel + Spinel-desktop build where fmrb_spx_kernel.c
# is not compiled. Log / Machine in fmrb_app_base_spinel.rb use these two.
module FmrbSpx
  ffi_func :fmrb_spx_board_millis, [], :int
  ffi_func :fmrb_spx_board_micros, [], :int
  ffi_func :fmrb_spx_log_write, [:int, :str, :int], :void
  ffi_func :fmrb_spx_theme_color, [:int], :int   # theme colour by fmrb_theme_t field index
end

module FmrbSpxGfx
  # --- basic drawing primitives (0 ok / neg err) ---
  ffi_func :fmrb_spx_gfx_clear,        [:int, :int], :int
  ffi_func :fmrb_spx_gfx_set_pixel,    [:int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_get_pixel,    [:int, :int, :int], :int   # >=0 color / neg
  ffi_func :fmrb_spx_gfx_draw_line,    [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_rect,    [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_fill_rect,    [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_blend_rect,   [:int, :int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_circle,  [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_fill_circle,  [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_round_rect, [:int, :int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_fill_round_rect, [:int, :int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_ellipse, [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_fill_ellipse, [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_triangle, [:int, :int, :int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_fill_triangle, [:int, :int, :int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_arc,     [:int, :int, :int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_fill_arc,     [:int, :int, :int, :int, :int, :int, :int, :int], :int

  # --- text ---
  ffi_func :fmrb_spx_gfx_set_text_size, [:int, :int], :int
  ffi_func :fmrb_spx_gfx_set_font,      [:int, :int, :int], :int
  # text passed as :str + explicit length; flags bit0=bg given, bit1=hybrid
  ffi_func :fmrb_spx_gfx_draw_text, [:int, :int, :int, :str, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_wallclock, [:int, :int, :int, :int, :int], :int   # 1 drawn / 0 clock unset
  ffi_func :fmrb_spx_gfx_draw_free_iram, [:int, :int, :int, :int, :int], :int

  ffi_func :fmrb_spx_gfx_present, [:int, :int, :int, :int], :int

  # --- CVBS/NTSC output control ---
  ffi_func :fmrb_spx_gfx_set_output_level, [:int, :int], :int
  ffi_func :fmrb_spx_gfx_set_chroma_level, [:int, :int], :int

  # --- composite regions / viewport / sprite clip ---
  # packed: :str of count records, 14 bytes each (7 int16 LE)
  ffi_func :fmrb_spx_gfx_set_composite_regions, [:int, :str, :int], :int
  ffi_func :fmrb_spx_gfx_set_canvas_viewport,   [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_set_sprite_clip,       [:int, :int, :int, :int, :int], :int

  # --- image API ---
  ffi_func :fmrb_spx_gfx_draw_image,   [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_delete_image, [:int, :int], :int
  # returns :binstr 6B (id,w,h u16 LE) or "" (nil)
  ffi_func :fmrb_spx_gfx_create_image_from_file, [:int, :str, :int], :binstr
  ffi_func :fmrb_spx_gfx_video_open,    [:int, :str, :int, :int, :int, :int, :int], :binstr
  ffi_func :fmrb_spx_gfx_video_control, [:int], :binstr
  ffi_func :fmrb_spx_gfx_video_status,  [], :binstr
  ffi_func :fmrb_spx_gfx_create_mask,  [:int, :int, :int, :str, :int], :int
  ffi_func :fmrb_spx_gfx_delete_mask,  [:int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_image_masked, [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_draw_tile,    [:int, :int, :int, :int, :int, :int, :int, :int], :int

  # --- file transfer ---
  ffi_func :fmrb_spx_gfx_sync_file,     [:str, :int, :str, :int], :int   # 1=ok
  ffi_func :fmrb_spx_gfx_transfer_file, [:str, :int, :str, :int], :int
  ffi_func :fmrb_spx_gfx_file_status,   [:str, :int], :int   # size>=0 / neg=absent

  # --- sprite API ---
  ffi_func :fmrb_spx_gfx_create_sprite_image, [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_delete_sprite_image, [:int, :int], :int
  ffi_func :fmrb_spx_gfx_load_sprite_image_bmp, [:int, :int, :str, :int], :int
  ffi_func :fmrb_spx_gfx_set_sprite_image_target, [:int, :int], :int
  # frames: :str of frame_count u16 LE image ids
  ffi_func :fmrb_spx_gfx_create_sprite_instance, [:int, :str, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_delete_sprite_instance, [:int, :int], :int
  ffi_func :fmrb_spx_gfx_sprite_move,    [:int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_sprite_visible, [:int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_sprite_frame,   [:int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_delete_all_sprites, [:int], :int
end

module FmrbSpxApp
  # --- instance lifecycle ---
  ffi_func :fmrb_spx_app_init, [], :binstr           # 50B ctx snapshot (creates canvas)
  # poll one message; payload as :binstr, type/src via out-params
  ffi_func :fmrb_spx_app_recv_message, [:int, :ptr, :ptr], :binstr
  ffi_func :fmrb_spx_app_cleanup, [], :int
  ffi_func :fmrb_spx_app_mark_expected_stop, [], :int
  ffi_func :fmrb_spx_app_send_message, [:int, :int, :str, :int], :int
  ffi_func :fmrb_spx_app_send_audio_note, [:int, :int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_app_set_window_param, [:int, :int], :int  # 0=pos_x 1=pos_y
  ffi_func :fmrb_spx_app_is_file_app, [], :int
  ffi_func :fmrb_spx_app_create_canvas, [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_app_delete_canvas, [:int], :int

  # --- class: process / memory info (all :binstr) ---
  ffi_func :fmrb_spx_app_ps, [], :binstr
  ffi_func :fmrb_spx_app_heap_info, [], :binstr
  ffi_func :fmrb_spx_app_sys_pool_info, [], :binstr
  ffi_func :fmrb_spx_app_pool_usage, [], :int
  ffi_func :fmrb_spx_app_gfx_stats, [], :binstr
  ffi_func :fmrb_spx_app_last_error, [], :binstr

  # --- class: configuration / clock ---
  ffi_func :fmrb_spx_app_config, [:str, :int], :binstr
  ffi_func :fmrb_spx_app_language, [], :binstr
  ffi_func :fmrb_spx_app_set_kana_mode, [:int], :int
  ffi_func :fmrb_spx_app_rd_stream_state, [], :int
  ffi_func :fmrb_spx_app_wallclock, [], :binstr
  ffi_func :fmrb_spx_app_set_wallclock, [:int, :int, :int, :int, :int, :int], :binstr

  # --- class: cursor / power ---
  ffi_func :fmrb_spx_app_enable_cursor, [], :int
  ffi_func :fmrb_spx_app_set_cursor_visible, [:int], :int
  ffi_func :fmrb_spx_app_reboot, [], :int
  ffi_func :fmrb_spx_app_ble_start, [], :int
  ffi_func :fmrb_spx_app_ble_state, [], :int
  ffi_func :fmrb_spx_app_wifi_connected, [], :int
  ffi_func :fmrb_spx_app_proc_generation, [], :int
  ffi_func :fmrb_spx_app_bt_mac, [], :binstr

  # --- class: network / usb ---
  ffi_func :fmrb_spx_app_wifi_info, [], :binstr
  ffi_func :fmrb_spx_app_clear_cache, [:str, :int], :binstr
  ffi_func :fmrb_spx_app_usb_devices, [], :binstr
  ffi_func :fmrb_spx_app_hid_raw_subscribe, [:int], :int
  ffi_func :fmrb_spx_app_hid_raw_unsubscribe, [:int], :int
  ffi_func :fmrb_spx_app_boot_complete, [], :int
  ffi_func :fmrb_spx_app_gc, [], :int                # force GC on this task's heap

  # --- out-params for recv_message (read back with read_i32) ---
  ffi_buffer :type_out, 4
  ffi_buffer :src_out, 4
  ffi_read_i32 :read_i32, 0
end

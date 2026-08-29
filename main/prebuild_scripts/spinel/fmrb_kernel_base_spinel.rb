# Spinel base layer for the kernel VM.
#
# The mruby kernel gets its FmrbKernel base class, Log, Machine and FmrbConst
# from C extensions (picoruby-fmrb-*). The Spinel kernel has no such layer, so
# this file re-implements them in Ruby on top of the FmrbSpx FFI shim
# (fmrb_ffi.rb / main/kernel/fmrb_spx.h). FmrbKernelImpl and the mixins sit on
# top unchanged -- the dual-build seam is confined here.
#
# Requires (spliced before this file in the combined build):
#   fmrb_ffi.rb (FmrbSpx), fmrb_const_generated.rb (FmrbConst), msgpack_pure.rb

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

  # Busy-ish wait: the kernel only uses delay_ms for retry backoff in
  # initial_sequence, so a coarse spin against board_millis is adequate and
  # avoids a FreeRTOS FFI. (Not for hot paths.)
  def self.delay_ms(ms)
    target = board_millis + ms
    while board_millis < target
      # spin
    end
    nil
  end
end

# Little-endian readers over a byte String (payload / packed records).
module SpxBytes
  def self.u16(s, off)
    s.getbyte(off) | (s.getbyte(off + 1) << 8)
  end

  # NUL-padded fixed-width name field -> String (stops at first NUL). Built with
  # setbyte rather than Integer#chr (the Spinel runtime has no sp_str_chr).
  # NOTE: must NOT be called `name` -- Spinel resolves `SpxBytes.name` to the
  # built-in Module#name (returning the string "SpxBytes") instead of this
  # method, silently corrupting every parsed field. Use a distinct verb.
  def self.read_name(s, off, width)
    len = 0
    while len < width && s.getbyte(off + len) != 0
      len += 1
    end
    out = "\x00" * len
    i = 0
    while i < len
      out.setbyte(i, s.getbyte(off + i))
      i += 1
    end
    out
  end
end

class FmrbKernel
  # ---- init / readiness ----
  def _init
    @tick = 33
    # From C, not a literal: this is system_conf.toml's max_apps clamped to the
    # build ceiling, and the literal that used to sit here went stale.
    @max_app_num = FmrbSpx.fmrb_spx_max_apps
    @max_path_len = 128
    nil
  end

  def _set_ready
    FmrbSpx.fmrb_spx_set_ready
    nil
  end

  # ---- messaging (control inversion) ----
  # Returns { type:, src_pid:, data: } (symbol keys, matching the mruby C
  # binding) or nil on timeout.
  #
  # The Hash is ONE reused instance, not a fresh allocation: dispatch is
  # strictly synchronous (msg_handler returns before the next poll), so a
  # single message is ever live, and building a 3-key symbol Hash per
  # message (~456B) was a top allocation on the kernel's hot path -- at
  # MIDI rates it meaningfully raised the GC cadence
  # (doc/spinel_aot/spinel_gc_notes.md). Consumers must not retain the
  # Hash (or the msg itself) across messages.
  def _poll_message(timeout_ms)
    data = FmrbSpx.fmrb_spx_recv_message(timeout_ms, FmrbSpx.type_out, FmrbSpx.src_out)
    type = FmrbSpx.read_i32(FmrbSpx.type_out)
    return nil if type < 0
    src = FmrbSpx.read_i32(FmrbSpx.src_out)
    @poll_msg = { type: 0, src_pid: 0, data: nil } unless @poll_msg
    msg = @poll_msg
    msg[:type] = type
    msg[:src_pid] = src
    msg[:data] = data
    msg
  end

  # data may be poly (e.g. msg[:data] from a symbol hash, or a msgpack result);
  # .to_s pins it to a concrete String so it can cross the :str FFI boundary
  # (Phase 0/T1-5: no poly at the FFI boundary). On a concrete String .to_s is
  # self.
  def _send_raw_message(pid, type, data)
    s = data.to_s
    FmrbSpx.fmrb_spx_send_raw(pid, type, s, s.bytesize) == 1
  end

  def _try_send_raw_message(pid, type, data)
    s = data.to_s
    FmrbSpx.fmrb_spx_try_send_raw(pid, type, s, s.bytesize) == 1
  end

  # ---- window list ----
  # Parse the packed window snapshot (48-byte records, layout in fmrb_spx.h)
  # into the same Array-of-Hash structure the mruby C _get_window_list returns.
  def _get_window_list
    buf = FmrbSpx.fmrb_spx_windows_snapshot   # :binstr, N * 48 bytes
    count = buf.bytesize / 48
    list = []
    return list if count <= 0
    i = 0
    while i < count
      base = i * 48
      flags = buf.getbyte(base + 2)
      list << {
        pid: buf.getbyte(base + 0),
        z_order: buf.getbyte(base + 1),
        fullscreen: (flags & 0x01) != 0,
        resizable: (flags & 0x02) != 0,
        x: SpxBytes.u16(buf, base + 4),
        y: SpxBytes.u16(buf, base + 6),
        width: SpxBytes.u16(buf, base + 8),
        height: SpxBytes.u16(buf, base + 10),
        min_width: SpxBytes.u16(buf, base + 12),
        min_height: SpxBytes.u16(buf, base + 14),
        app_name: SpxBytes.read_name(buf, base + 16, 32)
      }
      i += 1
    end
    list
  end

  # ---- focus / z-order / geometry ----
  def _set_hid_target(pid);           FmrbSpx.fmrb_spx_set_hid_target(pid) == 0; end
  def _set_focused_window(win_id);    FmrbSpx.fmrb_spx_set_focused_window(win_id) == 0; end
  def _bring_to_front(pid);           FmrbSpx.fmrb_spx_bring_to_front(pid) == 1; end
  def _update_window_position(pid, x, y); FmrbSpx.fmrb_spx_update_window_pos(pid, x, y) == 1; end
  def _update_window_size(pid, w, h);     FmrbSpx.fmrb_spx_update_window_size(pid, w, h) == 1; end
  # Runtime window <-> fullscreen switch. Spinel has no bool FFI arg, so the flag
  # crosses as an int.
  def _set_app_fullscreen(pid, on, w, h)
    FmrbSpx.fmrb_spx_set_app_fullscreen(pid, on ? 1 : 0, w, h) == 1
  end

  # ---- lifecycle ----
  def _suspend_app(pid); FmrbSpx.fmrb_spx_suspend_app(pid) == 0; end
  def _resume_app(pid);  FmrbSpx.fmrb_spx_resume_app(pid) == 0; end
  def _reap_app(pid);    FmrbSpx.fmrb_spx_reap_app(pid) == 0; end

  # nil on failure, with why in @last_spawn_err (an fmrb_err.h code, 0 when the
  # spawn worked). The mruby binding sets the same ivar, so the kernel reads it
  # the same way on both engines.
  def _spawn_app_req(name)
    s = name.to_s   # concrete String for the :str FFI boundary
    pid = FmrbSpx.fmrb_spx_spawn_app_req(s, s.bytesize)
    @last_spawn_err = pid < 0 ? pid : 0
    pid < 0 ? nil : pid
  end

  def _mark_expected_stop(pid)
    FmrbSpx.fmrb_spx_mark_expected_stop(pid)
    nil
  end

  # ---- app info ----
  # Packed record (fmrb_spx_app_info_snapshot):
  #   0: valid(1)  1: fullscreen(1)  2: vm_type(1)  3: load_mode(1)
  #   4: name (32, NUL-pad)  36: path (128, NUL-pad)
  #   164: fullscreen_switchable(1)  165: headless(1)  166: expected_stop(1)
  # Byte 2 -> vm_type symbol, indexed by the value fmrb_spx_kernel.c writes.
  # Keep in step with fmrb_vm_type_t when a VM type is added.
  APP_INFO_VM_TYPES = [:unknown, :mruby, :lua, :basic, :native, :micropython]

  def _get_app_info(pid)
    buf = FmrbSpx.fmrb_spx_app_info_snapshot(pid)   # :binstr, 167 bytes or ""
    return nil if buf.bytesize == 0 || buf.getbyte(0) == 0
    vm_idx = buf.getbyte(2)
    vm_sym = vm_idx < APP_INFO_VM_TYPES.size ? APP_INFO_VM_TYPES[vm_idx] : :unknown
    {
      fullscreen: buf.getbyte(1) != 0,
      vm_type: vm_sym,
      load_mode: buf.getbyte(3),
      name: SpxBytes.read_name(buf, 4, 32),
      path: SpxBytes.read_name(buf, 36, 128),
      fullscreen_switchable: buf.getbyte(164) != 0,
      headless: buf.getbyte(165) != 0,
      expected_stop: buf.getbyte(166) != 0
    }
  end

  # ---- errors / led ----
  def _get_last_error
    buf = FmrbSpx.fmrb_spx_last_error   # :binstr, 176 bytes or ""
    return nil if buf.bytesize == 0
    # name NUL error (two NUL-separated fields)
    name = SpxBytes.read_name(buf, 0, 64)
    err = SpxBytes.read_name(buf, 64, 112)
    { name: name, error: err }
  end

  def _set_error_led(level)
    FmrbSpx.fmrb_spx_set_error_led(level)
    nil
  end

  # ---- host handshake ----
  def check_protocol_version(timeout_ms = 5000)
    FmrbSpx.fmrb_spx_check_protocol_version(timeout_ms) == 1
  end

  def check_ga_version(timeout_ms = 5000)
    FmrbSpx.fmrb_spx_check_ga_version(timeout_ms) == 1
  end

  # ---- file / time sync ----
  # Reads the same system_conf.toml entries the mruby binding reads. This used to
  # answer with an empty list, which meant a Spinel kernel silently synced
  # nothing on the device. Entries come over one at a time (src 128, dest 128,
  # both NUL-padded) so the C side needs one 256 byte buffer rather than one for
  # every entry at once.
  SYNC_PATH_WIDTH = 128

  def _get_sync_files
    out = []
    count = FmrbSpx.fmrb_spx_sync_file_count
    i = 0
    while i < count
      buf = FmrbSpx.fmrb_spx_sync_file_entry(i)
      if buf.bytesize > 0
        out << { src: SpxBytes.read_name(buf, 0, SYNC_PATH_WIDTH),
                 dest: SpxBytes.read_name(buf, SYNC_PATH_WIDTH, SYNC_PATH_WIDTH) }
      end
      i += 1
    end
    out
  end

  def _sync_file(src, dest)
    s = src.to_s   # concrete String for the :str FFI boundary
    d = dest.to_s
    FmrbSpx.fmrb_spx_sync_file(s, s.bytesize, d, d.bytesize) == 1
  end

  def _sync_time_to_host
    FmrbSpx.fmrb_spx_sync_time_to_host
    nil
  end
end

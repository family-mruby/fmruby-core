# FFI declarations for the Spinel raycast backend (native/raycast_native.c).
#
# The entry takes no arguments (`int f(void)`), so everything crosses here.
# Nothing but bytes and ints does: the map as a binary String (:binstr,
# byte-exact via sp_net_bin_len), the player as three ints, the depth buffer
# back as a String plus its length. No Float touches the boundary -- the
# raycaster is fixed-point throughout, which is why it suits this pattern.
#
# The map is separate from the per-frame input because it changes rarely and
# the core built against it is expensive to rebuild. raycast_spx_map_gen is how
# the entry finds out that it did change: the receiver bumps it on every
# upload, and the cached core carries the generation it was built for.
module RaycastSpx
  ffi_func :raycast_spx_map,     [], :binstr
  ffi_func :raycast_spx_map_w,   [], :int
  ffi_func :raycast_spx_map_h,   [], :int
  ffi_func :raycast_spx_map_gen, [], :int

  ffi_func :raycast_spx_px, [], :int
  ffi_func :raycast_spx_py, [], :int
  ffi_func :raycast_spx_pa, [], :int

  ffi_func :raycast_spx_micros, [], :int

  ffi_func :raycast_spx_output, [:str, :int, :int], :void
  ffi_func :raycast_spx_log,    [:str, :int], :void
end

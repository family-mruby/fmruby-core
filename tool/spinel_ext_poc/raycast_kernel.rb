# Raycast as an upstream Spinel ext library (P1 prototype, not wired into any build).
#
# Compiled with
#   spinel raycast_kernel.rb -c --ext-init Init_raycast \
#     --ext-entry RaycastKernel.load_map,RaycastKernel.cast
# which emits raycast_kernel.c plus the contract header raycast_kernel.h. The
# host calls Init_raycast() once, then the entries as plain typed C functions:
#
#   sp_int       sp_RaycastKernel_s_load_map(const char *map, sp_int w, sp_int h);
#   const char * sp_RaycastKernel_s_cast(sp_int px, sp_int py, sp_int pa);
#
# Compared with the fork's gem (spinel/raycast_entry.rb + raycast_ffi.rb):
#   - no FFI getters/setters: the map and the player arrive as arguments and the
#     packed depth buffer is the return value (a Spinel String keeps embedded
#     NULs, so no :binstr and no sp_net_bin_len)
#   - no --persistent-statics and no generation counter: the core lives in a
#     module instance variable, which survives between entry calls because the
#     top level runs only once (in Init_raycast). Replacing the map is simply
#     calling load_map again.
#   - a bad map raises ArgumentError, which the host receives through
#     Init_raycast_try as (class name, message) instead of a log FFI.
#
# raycast_core.rb is read from the gem's mrblib in place (one source, three
# engines: mruby, the fork's Spinel, and this).
require_relative "../../lib/add/picoruby-fmrb-raycast/mrblib/raycast_core"

module RaycastKernel
  # Build (or rebuild) the core for a map of w*h bytes, one byte per cell.
  # Returns the number of cells accepted.
  def self.load_map(map, w, h)
    if w < 1 || h < 1 || map.bytesize < w * h
      raise ArgumentError, "bad map w=#{w} h=#{h} bytes=#{map.bytesize}"
    end
    @core = RaycastCore.new(map, w, h, 0)
    w * h
  end

  # One frame of rays, packed as RaycastCore::RAY_BYTES bytes per ray. The
  # returned buffer is the core's own and is overwritten by the next call.
  def self.cast(px, py, pa)
    core = @core
    raise RuntimeError, "load_map has not been called" if core.nil?
    core.cast_packed(px, py, pa)
  end
end

# Type inference driver: the entries' argument types are taken from these calls.
# Excluded from Init_raycast.
if __FILE__ == $0
  RaycastKernel.load_map("\x01\x00\x00\x01" * 4, 4, 4)
  p RaycastKernel.cast(384, 384, 45).bytesize
end

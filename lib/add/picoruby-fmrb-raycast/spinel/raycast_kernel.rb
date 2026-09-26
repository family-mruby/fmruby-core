# spinel-ext-init: Init_raycast
# spinel-ext-entry: RaycastKernel.load_map,RaycastKernel.cast
#
# The Spinel side of the raycast comparison: the same raycast_core.rb the mruby
# VM runs, compiled as a Spinel ext program and called from an mruby app task
# as a library (native/raycast_native.c explains how that is possible).
#
# rake spinel:gen reads the two lines above and runs
#
#   spinel raycast_kernel.rb -c --ext-init Init_raycast \
#     --ext-entry RaycastKernel.load_map,RaycastKernel.cast
#
# which emits raycast_kernel.c and the header raycast_kernel.h with the
# entries as typed C functions:
#
#   sp_int       sp_RaycastKernel_s_load_map(const char *lv_map, sp_int lv_w, sp_int lv_h);
#   const char * sp_RaycastKernel_s_cast(sp_int lv_px, sp_int lv_py, sp_int lv_pa);
#
# The core lives in a module instance variable. The top level runs once, in
# Init_raycast, so @core survives between entry calls for as long as the
# instance does -- building it means 720 Math.sin/Math.cos calls for the trig
# tables, far more than one frame of rays. Replacing the map is calling
# load_map again. A new instance starts with @core nil (its init clears the
# program's statics), so a stale core from a destroyed heap cannot be reached.
#
# raycast_core.rb is copied next to this file by `rake spinel:gen` from the
# gem's mrblib -- one file, two engines, so the comparison cannot drift apart
# through an edit to one copy.
require_relative "raycast_core"

module RaycastKernel
  # Build the core for a map of w*h bytes, one byte per cell. Returns the
  # number of cells taken.
  def self.load_map(map, w, h)
    if w < 1 || h < 1 || map.bytesize < w * h
      raise ArgumentError, "bad map w=#{w} h=#{h} bytes=#{map.bytesize}"
    end
    @core = RaycastCore.new(map, w, h, 0)
    w * h
  end

  # One frame of rays, packed RaycastCore::RAY_BYTES bytes a ray. The String is
  # the core's own buffer and is overwritten by the next call.
  def self.cast(px, py, pa)
    core = @core
    raise RuntimeError, "load_map has not been called" if core.nil?
    core.cast_packed(px, py, pa)
  end
end

# Type inference driver: the entries' argument types are taken from these
# calls. Left out of Init_raycast.
if __FILE__ == $0
  RaycastKernel.load_map("\x01\x00\x00\x01" * 4, 4, 4)
  p RaycastKernel.cast(384, 384, 45).bytesize
end

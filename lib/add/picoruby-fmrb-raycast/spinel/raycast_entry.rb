# The Spinel side of the raycast comparison: the same raycast_core.rb the mruby
# VM runs, compiled to native code and called from an mruby app task as a
# library (native/raycast_native.c explains how that is possible).
#
# Compiled with `spinel --no-main --entry raycast_entry --persistent-statics`,
# so this file's top level IS the entry: it runs once per raycast_run() call,
# which is once per frame.
#
# THE CORE IS CACHED IN A GLOBAL, and that is the reason --persistent-statics
# is on. Building a core means 720 Math.sin/Math.cos calls for the trig tables;
# doing that per frame would cost more than the rays it prepares for, and the
# FFT gem measured exactly that mistake (doc/mic_spectrum/report/track_a.md,
# E5/E6). With the flag, the entry's globals survive between calls and live as
# long as the Spinel instance -- which is what an ordinary Ruby object in a gem
# would do.
#
# The cache key is the cached object's own map_gen, never a second global
# holding the generation. The per-instance clear that --persistent-statics
# leaves in place nulls object globals but leaves integer ones alone, so a
# remembered generation would still match after the core it described had been
# swept, and the next frame would read through a null. Asking the object means
# the nil check comes first and short-circuits.
#
# raycast_core.rb is copied here by `rake spinel:gen` from the gem's mrblib --
# one file, two engines, so the comparison cannot drift apart through an edit
# to one copy. No caching lives in the core itself; it stays the plain Ruby the
# :ruby backend also runs.
require_relative "raycast_core"
require_relative "raycast_ffi"

gen = RaycastSpx.raycast_spx_map_gen
core = $raycast
if core.nil? || core.map_gen != gen
  map = RaycastSpx.raycast_spx_map
  w = RaycastSpx.raycast_spx_map_w
  h = RaycastSpx.raycast_spx_map_h
  if w < 1 || h < 1 || map.bytesize < w * h
    msg = "bad map w=#{w} h=#{h} bytes=#{map.bytesize}"
    RaycastSpx.raycast_spx_log(msg, msg.bytesize)
    core = nil
  else
    core = RaycastCore.new(map, w, h, gen)
    $raycast = core
  end
end

if !core.nil?
  px = RaycastSpx.raycast_spx_px
  py = RaycastSpx.raycast_spx_py
  pa = RaycastSpx.raycast_spx_pa

  t0 = RaycastSpx.raycast_spx_micros
  buf = core.cast_packed(px, py, pa)
  us = RaycastSpx.raycast_spx_micros - t0

  RaycastSpx.raycast_spx_output(buf, buf.bytesize, us)
end

0

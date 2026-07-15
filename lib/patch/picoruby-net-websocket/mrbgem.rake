# Family mruby patch of picoruby-net-websocket/mrbgem.rake.
#
# Upstream points the mruby-VM pack dependency at
# picoruby-mruby/lib/mruby/mrbgems/picoruby-pack, which does not exist in the
# vendored mruby tree (the gem there is mruby-pack). Fix the gemdir so the
# esp32p4/linux (microruby) builds resolve it; `require 'pack'` then succeeds
# because mruby-* gems are registered in prebuilt_gems[] as "pack".
MRuby::Gem::Specification.new('picoruby-net-websocket') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'WebSocket client and server for PicoRuby'

  spec.require_name = 'net/websocket'

  if build.picoruby?
    spec.add_dependency 'mruby-pack', gemdir: "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-pack"
  elsif build.femtoruby?
    spec.add_dependency 'picoruby-pack'
  end
  spec.add_dependency 'picoruby-socket'
  spec.add_dependency 'picoruby-base64'
  spec.add_dependency 'picoruby-rng'
  spec.add_dependency 'picoruby-mbedtls'
end

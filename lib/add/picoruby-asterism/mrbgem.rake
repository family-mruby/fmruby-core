# picoruby-asterism: call Ruby objects on other machines (doc/ruby_asterism, A1).
#
# Pure Ruby (mrblib only). Built on Asterism::Zenoh (picoruby-asterism-zenoh)
# and a MessagePack module with pack / unpack (the msgpack gem's API; any gem
# that provides it will do). Also a minimal ROS 2 (rmw_zenoh) node,
# Asterism::ROS, with its CDR encoding, Asterism::CDR (R1; these need only
# Asterism::Zenoh). Nothing here is specific to Family mruby.
MRuby::Gem::Specification.new('picoruby-asterism') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Asterism: proxies for Ruby objects on other machines, over Zenoh'
  spec.add_dependency 'picoruby-asterism-zenoh'
end

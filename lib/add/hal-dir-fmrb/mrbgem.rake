# hal-dir-fmrb: a name-only HAL provider for mruby-dir.
#
# It ships NO C sources. Like hal-task-freertos, its sole purpose is its name:
# MRuby::Gem::List#resolve_external_hal! matches `hal-<short>-*` against each
# gem (short = the target gem's last name segment, "dir" for mruby-dir), so
# loading this gem makes mruby-dir drop its ports/posix/dir_hal.c from the
# rake build.
#
# The native Linux build (lib/add/family_mruby_native.rb, doc/linux_native/)
# needs this because that posix port resolves every path under one flat
# "flash/" directory, while the native namespace is spread over several host
# roots (the distribution tree, the user's directory, the cache). Its
# directory HAL is lib/add/hal-esp32-dir/dir_hal.c instead, compiled by
# native/CMakeLists.txt against the file HAL's resolver -- the same file the
# ESP32 targets use.
MRuby::Gem::Specification.new('hal-dir-fmrb') do |spec|
  spec.license = 'MIT'
  spec.author  = 'family-mruby'
  spec.summary = 'Name-only HAL provider that hands the mruby-dir HAL to ' \
                 'the file HAL resolver (compiled by the native CMake build).'
end

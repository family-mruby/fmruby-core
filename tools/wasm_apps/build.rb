#!/usr/bin/env ruby
# frozen_string_literal: true
#
# Build the firmware's own wasm test apps (doc/wasm_apps W1b) into
# flash/app/test. They are written in C against the generated SDK header
# (sdk/c/fmrb_boundary.h) and built with zig cc (decision 12):
#
#   ruby tools/wasm_apps/build.rb
#
# ZIG: the zig binary (default: the newest ~/toolchains/zig-*/zig).
require 'fileutils'

ROOT = File.expand_path('../..', __dir__)
zig = ENV['ZIG'] || Dir[File.expand_path('~/toolchains/zig-*/zig')].max
abort 'no zig (set ZIG=...)' unless zig && File.executable?(zig)
cache = File.expand_path('~/toolchains/cache')
env = { 'ZIG_GLOBAL_CACHE_DIR' => File.join(cache, 'zig'), 'ZIG_LOCAL_CACHE_DIR' => File.join(cache, 'zig-local') }
out_dir = File.join(ROOT, 'flash', 'app', 'test')
%w[c_check].each do |name|
  out = File.join(out_dir, "#{name}.app.wasm")
  cmd = [zig, 'cc', '-target', 'wasm32-freestanding', '-nostdlib', '-Os',
         "-I#{File.join(ROOT, 'sdk', 'c')}", '-Wl,--no-entry', '-Wl,-z,stack-size=16384',
         File.join(__dir__, name, 'app.c'), '-o', out]
  system(env, *cmd) or abort "#{name}: build failed"
  puts "#{out} (#{File.size(out)} bytes)"
end

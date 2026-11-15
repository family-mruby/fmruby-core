# rakelib/boundary.rake
# The app boundary (doc/app_boundary): the glue of every engine and the SDK
# headers, generated from the classes marked %a{fmrb.boundary} in sig/*.rbs
# (every method of theirs but the ones marked %a{fmrb.internal} / fmrb.hook).
# Host ruby with the rbs gem, before docker, like ti:gen. The top-level setup
# task runs boundary:gen, so every build (Linux, esp32, native, browser)
# regenerates the firmware's glue first; only a changed file is rewritten.

namespace :boundary do
  desc "Generate the boundary glue (firmware, not committed) and SDK (committed) from sig/"
  task :gen do
    sh "#{RbConfig.ruby} tools/boundary/gen.rb"
  end

  desc "Check that the committed boundary SDK and MicroPython's qstr table match sig/"
  task :check do
    sh "#{RbConfig.ruby} tools/boundary/gen.rb --check"
  end

  # The SDK repository takes boundary.json and the C header (English only)
  # from here and generates the language bindings itself (doc/wasm_apps W5).
  desc "Write the boundary into a family-mruby-sdk checkout (SDK=dir, default ../family-mruby-sdk)"
  task :sdk do
    dir = ENV['SDK'] || File.expand_path('../family-mruby-sdk', Dir.pwd)
    sh "#{RbConfig.ruby} tools/boundary/gen.rb --sdk #{dir}"
  end
end

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
end

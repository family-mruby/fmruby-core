MRuby::Gem::Specification.new('picoruby-fmrb-kernel') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Family mruby OS Kernel API for PicoRuby'

  spec.add_dependency 'picoruby-gpio'
  spec.add_dependency 'picoruby-machine'

  # C extension files
  spec.objs = Dir.glob("#{dir}/src/*.{c,cpp,cxx,cc}").map { |f|
    objfile f.relative_path_from(dir).pathmap("#{build_dir}/%X")
  }

  # Include directories for C extensions
  spec.cc.include_paths << "#{dir}/src"

  # Add project paths for fmrb_hal, fmrb_ipc headers
  # dir is components/picoruby-esp32/picoruby/mrbgems/picoruby-fmrb-kernel
  # We need to go back to project root
  spec.cc.include_paths << "#{dir}/../../../../../main/lib/fmrb_hal"
  spec.cc.include_paths << "#{dir}/../../../../../main/lib/fmrb_ipc"
  spec.cc.include_paths << "#{dir}/../../../../../main/include"

  # Compiler flags
  spec.cc.flags << '-DFMRB_PICORUBY_BINDING'
end

MRuby::Gem::Specification.new('picoruby-fmrb') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Family mruby Hardware Abstraction Layer for PicoRuby'

  spec.add_dependency 'picoruby-gpio'
  spec.add_dependency 'picoruby-machine'

  # C extension files
  spec.objs = Dir.glob("#{dir}/src/*.{c,cpp,cxx,cc}")

  # Include directories for C extensions
  spec.cc.include_paths << "#{dir}/src"
  spec.cc.include_paths << "#{build_dir}/../main/lib/fmrb_hal"
  spec.cc.include_paths << "#{build_dir}/../main/lib/fmrb_ipc"
  spec.cc.include_paths << "#{build_dir}/../main/lib/fmrb_gfx"
  spec.cc.include_paths << "#{build_dir}/../main/lib/fmrb_audio"

  # Compiler flags
  spec.cc.flags << '-DFMRB_PICORUBY_BINDING'
end
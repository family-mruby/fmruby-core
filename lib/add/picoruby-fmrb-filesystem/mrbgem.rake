MRuby::Gem::Specification.new('picoruby-fmrb-filesystem') do |spec|
  spec.license = 'MIT'
  spec.author  = 'FMRuby Project'
  spec.summary = 'Filesystem for FMRuby using fmrb_hal'

  # Add include paths for fmrb components
  # Use absolute path from project root
  # dir is mrbgems/picoruby-fmrb-filesystem, so we need to go up 5 levels to get to project root
  project_root = File.expand_path("#{dir}/../../../../..")

  spec.cc.include_paths << "#{project_root}/components/fmrb_hal"
  spec.cc.include_paths << "#{project_root}/components/fmrb_common/include"

  # No dependencies on other filesystem gems
  spec.add_conflict 'picoruby-filesystem-fat'
  spec.add_conflict 'picoruby-vfs'
  spec.add_conflict 'picoruby-posix-io'
end

MRuby::Gem::Specification.new('picoruby-fmrb-filesystem') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Katsuhiko Kageyama'
  spec.summary = 'Filesystem for Family mruby using fmrb_hal'

  spec.add_dependency 'picoruby-fmrb-io'

  # No dependencies on other filesystem gems
  spec.add_conflict 'picoruby-filesystem-fat'
  spec.add_conflict 'picoruby-vfs'
  spec.add_conflict 'picoruby-posix-io'
end

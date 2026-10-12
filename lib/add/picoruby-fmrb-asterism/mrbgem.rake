# Family mruby's defaults for Asterism (doc/ruby_asterism, V2.5): the node
# profile every application VM starts with. Pure Ruby; listed right after
# picoruby-asterism in the builds that have Asterism.
MRuby::Gem::Specification.new('picoruby-fmrb-asterism') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = "Family mruby's default Asterism node profile"

  spec.add_dependency 'picoruby-fmrb-const'
  spec.add_dependency 'picoruby-asterism'
end

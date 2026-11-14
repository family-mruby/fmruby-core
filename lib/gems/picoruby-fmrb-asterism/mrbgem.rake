# Family mruby's defaults for Asterism (doc/ruby_asterism, V2.5, G1b): the node
# profile every application VM starts with, and the machine's saved name
# (FmrbAsterism.save_name). Pure Ruby; listed right after
# picoruby-asterism in the builds that have Asterism.
MRuby::Gem::Specification.new('picoruby-fmrb-asterism') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = "Family mruby's default Asterism node profile"

  spec.add_dependency 'picoruby-fmrb-const'
  spec.add_dependency 'picoruby-asterism'
  # FmrbToml reads config.toml when this gem's file runs (the saved name).
  spec.add_dependency 'picoruby-fmrb-app'
end

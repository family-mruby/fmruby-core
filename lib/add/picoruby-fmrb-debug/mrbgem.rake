MRuby::Gem::Specification.new('picoruby-fmrb-debug') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'On-device debugger API (FMRB::Debug) for Family mruby'

  # MessagePack.unpack decodes the inspect payloads (stack_trace/frame_vars/
  # expand) returned as raw msgpack bodies by the C bindings.
  spec.add_dependency 'picoruby-fmrb-msgpack'
end

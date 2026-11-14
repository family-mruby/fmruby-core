MRuby::Gem::Specification.new('picoruby-machine') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'Machine class'

  spec.add_dependency 'picoruby-require'
  # family-mruby: picoruby-io-console is NOT used; picoruby-fmrb-io provides IO.
  # spec.add_dependency 'picoruby-io-console'

  if build.posix?
    # The POSIX port runs a dedicated stdin reader thread to emulate the
    # UART/CDC RX interrupt used on bare-metal targets (see ports/posix/machine.c).
    spec.cc.flags << '-pthread'
    spec.linker.flags << '-pthread'
  end

  spec.cc.include_paths << "#{dir}/include"
  if build.vm_mruby?
    # mruby-task include path for task.h / task_hal.h (the freertos port and the
    # mrb->task.switching field), and the mruby include path for mruby.h.
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/include"
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/include"
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/include"
  end

  if build.posix?
    cc.defines << "PICORB_PLATFORM_POSIX"
  end

  # family-mruby: mruby-io is NOT used; picoruby-fmrb-io provides IO (mruby-io
  # conflicts with picoruby-fmrb-io). Upstream's conditional posix
  # `add_dependency 'mruby-io'` is deliberately dropped.
end

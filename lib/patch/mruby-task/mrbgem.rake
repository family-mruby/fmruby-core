MRuby::Gem::Specification.new('mruby-task') do |spec|
  spec.license = 'MIT'
  spec.authors = 'mruby developers'
  spec.summary = 'Cooperative multitasking with preemptive scheduling'

  # Enable task scheduler globally (required for vm.c integration)
  spec.build.defines << 'MRB_USE_TASK_SCHEDULER'

  # family-mruby: Task HAL functions are provided by ports/posix/hal.c
  # and ports/esp32/machine.c using FreeRTOS task-based tick delivery.
  # No HAL gem auto-loading needed.
end

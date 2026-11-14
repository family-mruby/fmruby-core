# hal-task-freertos: a name-only HAL provider for mruby-task.
#
# It ships NO C sources. Its sole purpose is its name: MRuby::Gem::List#
# resolve_external_hal! matches `hal-<short>-*` against each gem (short =
# the target gem's last name segment), so loading this gem makes mruby-task
# (short "task") drop its own ports/*/task_hal.c objects from the build.
#
# family-mruby needs this because the mruby-task HAL is the FreeRTOS case-D
# tick port (ports/freertos/task_hal.c), which uses FreeRTOS headers and is
# therefore compiled by the CMake/ESP-IDF side (PICORUBY_SRCS), NOT by the
# rake mruby build. Without this stub, conf.ports :posix would make the rake
# build compile mruby-task/ports/posix/task_hal.c, whose SIGALRM + setitimer
# timer collides with the Linux FreeRTOS POSIX simulator's own signal-based
# scheduler (see doc/work_picoruby_merge/instruct_d7_b1_tick.md section 3.5)
# and would also be a second, racing tick source against case-D.
MRuby::Gem::Specification.new('hal-task-freertos') do |spec|
  spec.license = 'MIT'
  spec.author  = 'family-mruby'
  spec.summary = 'Name-only HAL provider that hands the mruby-task HAL to the ' \
                 'FreeRTOS case-D port (compiled via CMake PICORUBY_SRCS).'
end

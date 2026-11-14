/*
** task_hal.c - family-mruby posix port stub.
**
** With conf.ports :posix the rake mruby build compiles this file, but the real
** mruby-task HAL for family-mruby is the FreeRTOS case-D port
** (ports/freertos/task_hal.c), compiled on the CMake/ESP-IDF side where the
** FreeRTOS headers live (component PRIV_REQUIRES freertos; see
** components/picoruby-esp32/CMakeLists.txt PICORUBY_SRCS).
**
** So this posix port must define NO symbols: the upstream version installs a
** SIGALRM/setitimer timer that (a) duplicates the freertos port's
** mrb_hal_task_* symbols at link time and (b) collides with the Linux FreeRTOS
** POSIX simulator's own signal-based scheduler (see
** doc/work_picoruby_merge/instruct_d7_b1_tick.md section 3.5). Keep it empty.
*/

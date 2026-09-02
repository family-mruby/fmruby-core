#ifndef PICORUBY_FMRB_CONST_H
#define PICORUBY_FMRB_CONST_H

#include <stdint.h>
#include <mruby.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The system theme is not here: it moved to fmrb_common's fmrb_theme.h,
   because Lua and MicroPython draw window frames too and neither can include
   this header (it pulls in mruby.h). Include that one to read the colours;
   this gem only turns them into FmrbConst::THEME_*. It cannot be included
   from here either -- src/picoruby_fmrb_const.c is built by the rake picoruby
   build, which has no firmware include paths. */

/* Rows one wheel notch scrolls (system_conf.toml wheel_lines). Set before any
   VM is created, read by apps as FmrbConst::WHEEL_LINES. */
void fmrb_wheel_lines_set(uint8_t lines);
uint8_t fmrb_wheel_lines_get(void);

void mrb_picoruby_fmrb_const_init_impl(mrb_state *mrb);

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_FMRB_CONST_H */

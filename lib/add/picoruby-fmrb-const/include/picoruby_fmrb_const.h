#ifndef PICORUBY_FMRB_CONST_H
#define PICORUBY_FMRB_CONST_H

#include <stdint.h>
#include <mruby.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System theme colors (RGB332 format) */
typedef struct {
    uint8_t desktop_bg;     /* Desktop wallpaper background */
    uint8_t menu_bg;        /* Menu bar / title bar background */
    uint8_t window_bg;      /* Window / dialog background */
    uint8_t text;           /* Normal text color */
    uint8_t text_light;     /* Light text (on dark backgrounds) */
    uint8_t highlight;      /* Selection / highlight */
    uint8_t border;         /* Window border / separator */
    uint8_t button;         /* Button background */
    uint8_t dir_color;      /* Directory text color */
} fmrb_theme_t;

/* Set theme (call before any VM is created, e.g. from TOML loader) */
void fmrb_theme_set(const fmrb_theme_t *theme);

/* Get current theme */
const fmrb_theme_t* fmrb_theme_get(void);

/* Rows one wheel notch scrolls (system_conf.toml wheel_lines). Set before any
   VM is created, read by apps as FmrbConst::WHEEL_LINES. */
void fmrb_wheel_lines_set(uint8_t lines);
uint8_t fmrb_wheel_lines_get(void);

void mrb_picoruby_fmrb_const_init_impl(mrb_state *mrb);

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_FMRB_CONST_H */

#ifndef FMRB_THEME_H
#define FMRB_THEME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file fmrb_theme.h
 * @brief The system colour theme, as every language sees it.
 *
 * One table, set once from [theme] in system_conf.toml before any VM starts,
 * read by whatever draws. It lives here rather than beside the Ruby constants
 * because Lua and MicroPython need it too and cannot include an mruby header:
 * picoruby_fmrb_const.h includes this one and adds the mruby bindings on top.
 *
 * Colours are RGB332 bytes, the same as everywhere else in the graphics API.
 */
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

/** Set the theme. Call before any VM is created (the TOML loader does). */
void fmrb_theme_set(const fmrb_theme_t *theme);

/** The theme in force. Never NULL: there is a built-in default. */
const fmrb_theme_t *fmrb_theme_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_THEME_H */

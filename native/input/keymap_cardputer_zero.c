/*
 * keymap_cardputer_zero.c - the Cardputer Zero's built-in keyboard
 * (keymap_extra.h).
 *
 * The keyboard is a TCA8418 matrix with three layers, all switched inside
 * the kernel driver (M5Stack's tca8418_keypad_m5stack, CardputerZero
 * overlay of m5stack-linux-dtoverlays):
 *
 *   linux,keymap   the plain layer: letters, digits, Esc, Tab, Enter,
 *                  Backspace, Space, Shift, Ctrl, Alt -- ordinary codes
 *   linux,keymap2  Fn: F1-F12, arrows, Home/End/PgUp/PgDn, Insert, Delete,
 *                  mute and volume -- ordinary codes as well
 *   linux,keymap1  Sym: the symbols. These arrive as codes borrowed from
 *                  other keys (KEY_LEFTBRACE for "!", KEY_NUMLOCK for "`",
 *                  KEY_KP7 for "-" ...) and are turned into characters by
 *                  the console key table tca8418_keypad_m5stack_keymap.map
 *                  ("keycode 26 = exclam"). That table is this one.
 *
 * The Sym and Fn keys themselves report KEY_CD (0x17f) and KEY_DVD (0x185);
 * they only switch layers, so they are swallowed. Codes the Sym layer leaves
 * alone (Esc, Tab, Enter, Backspace, Space, Ctrl, Alt) go through
 * linux_keys.c as usual.
 */

#include "keymap_extra.h"

#include <linux/input-event-codes.h>
#include <string.h>

typedef struct {
    uint16_t code;     /* what the Sym layer sends */
    uint8_t usage;     /* the US key that types the symbol */
    uint8_t shift;
    char ch;           /* the symbol (for the tests and the reader) */
} sym_key_t;

/* Physical key positions in the comments are the plain layer's. */
static const sym_key_t s_sym[] = {
    { KEY_LEFTBRACE,         0x1E, 1, '!' },   /* 1 */
    { KEY_RIGHTBRACE,        0x1F, 1, '@' },   /* 2 */
    { KEY_SEMICOLON,         0x20, 1, '#' },   /* 3 */
    { KEY_APOSTROPHE,        0x21, 1, '$' },   /* 4 */
    { KEY_GRAVE,             0x22, 1, '%' },   /* 5 */
    { KEY_BACKSLASH,         0x23, 1, '^' },   /* 6 */
    { KEY_COMMA,             0x24, 1, '&' },   /* 7 */
    { KEY_DOT,               0x25, 1, '*' },   /* 8 */
    { KEY_SLASH,             0x26, 1, '(' },   /* 9 */
    { KEY_MUHENKAN,          0x27, 1, ')' },   /* 0 */
    { KEY_KPASTERISK,        0x35, 1, '~' },   /* Q */
    { KEY_NUMLOCK,           0x35, 0, '`' },   /* W */
    { KEY_SCROLLLOCK,        0x2D, 1, '_' },   /* E */
    { KEY_KP7,               0x2D, 0, '-' },   /* R */
    { KEY_KP8,               0x2E, 1, '+' },   /* T */
    { KEY_KP9,               0x2E, 0, '=' },   /* Y */
    { KEY_KPMINUS,           0x2F, 0, '[' },   /* U */
    { KEY_KP4,               0x30, 0, ']' },   /* I */
    { KEY_KP5,               0x2F, 1, '{' },   /* O */
    { KEY_KP6,               0x30, 1, '}' },   /* P */
    { KEY_KP1,               0x33, 0, ';' },   /* A */
    { KEY_KP2,               0x33, 1, ':' },   /* S */
    { KEY_KP3,               0x34, 0, '\'' },  /* D */
    { KEY_KP0,               0x34, 1, '"' },   /* G */
    { KEY_KPDOT,             0x36, 1, '<' },   /* H */
    { KEY_ZENKAKUHANKAKU,    0x37, 1, '>' },   /* J */
    { KEY_102ND,             0x31, 0, '\\' },  /* K */
    { KEY_RO,                0x31, 1, '|' },   /* L */
    { KEY_KATAKANA,          0x36, 0, ',' },   /* V */
    { KEY_HIRAGANA,          0x37, 0, '.' },   /* B */
    { KEY_HENKAN,            0x38, 0, '/' },   /* N */
    { KEY_KATAKANAHIRAGANA,  0x38, 1, '?' },   /* M */
    /* The layer keys. */
    { KEY_CD,                0,    0, 0 },     /* Sym */
    { KEY_DVD,               0,    0, 0 },     /* Fn */
};

bool fmrb_keymap_cardputer_zero(unsigned code, uint8_t *usage, bool *shift)
{
    for (size_t i = 0; i < sizeof(s_sym) / sizeof(s_sym[0]); i++) {
        if (s_sym[i].code == code) {
            *usage = s_sym[i].usage;
            *shift = s_sym[i].shift != 0;
            return true;
        }
    }
    return false;
}

fmrb_keymap_extra_fn fmrb_keymap_extra_find(const char *name)
{
    if (name == NULL || name[0] == '\0') return NULL;
    if (strcmp(name, "cardputer_zero") == 0) return fmrb_keymap_cardputer_zero;
    return NULL;
}

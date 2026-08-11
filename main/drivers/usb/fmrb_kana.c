/**
 * @file fmrb_kana.c
 * @brief Romaji to kana composition (see fmrb_kana.h).
 *
 * The conversion rules started from the BASIC interpreter's kana_translate
 * (components/basic/core/basic_screen.cpp) and grew the parts a text editor
 * needs: precomposed dakuten, youon, sokuon, the long vowel mark and the two
 * Japanese punctuation marks. Output is Unicode, not the BASIC screen's own
 * character codes.
 */

#include "fmrb_kana.h"
#include "fmrb_keymap.h"

#include <string.h>

// Scancodes we act on. 0x35 is the half/full-width key, dead in the JP keymap
// (fmrb_keymap.c has it as {0,0}); 0x88 is the katakana/hiragana key, which
// the keymap currently spells as ']' and which we only take over while kana
// mode is on.
#define SC_ZENKAKU   0x35
#define SC_KANA      0x88
#define SC_SPACE     0x2C
#define SC_BACKSPACE 0x2A
#define SC_ESCAPE    0x29

#define KANA_N     0x3093  // ん
#define KANA_SOKU  0x3063  // っ
#define KANA_CHOON 0x30FC  // ー (katakana block, used in both modes)
#define KANA_TOUTEN 0x3001 // 、
#define KANA_KUTEN  0x3002 // 。

// Hiragana block a codepoint has to fall in to have a katakana counterpart.
#define HIRA_FIRST 0x3041
#define HIRA_LAST  0x3096
#define KATA_SHIFT 0x60

static const uint16_t k_vowels[5] = {0x3042, 0x3044, 0x3046, 0x3048, 0x304A};

// Small vowels used by youon: ya row for a/u/o, small i/e for the rest, which
// is what every IME does for "kyi" / "kye".
static const uint16_t k_small[5] = {0x3083, 0x3043, 0x3085, 0x3047, 0x3087};

typedef struct {
    char consonant;
    bool youon;        // can take a small vowel after "y"
    uint16_t cp[5];    // a i u e o, 0 where the sound does not exist
} kana_row_t;

static const kana_row_t k_rows[] = {
    {'k', true,  {0x304B, 0x304D, 0x304F, 0x3051, 0x3053}},
    {'s', true,  {0x3055, 0x3057, 0x3059, 0x305B, 0x305D}},
    {'t', true,  {0x305F, 0x3061, 0x3064, 0x3066, 0x3068}},
    {'n', true,  {0x306A, 0x306B, 0x306C, 0x306D, 0x306E}},
    {'h', true,  {0x306F, 0x3072, 0x3075, 0x3078, 0x307B}},
    {'m', true,  {0x307E, 0x307F, 0x3080, 0x3081, 0x3082}},
    {'y', false, {0x3084, 0,      0x3086, 0,      0x3088}},
    {'r', true,  {0x3089, 0x308A, 0x308B, 0x308C, 0x308D}},
    {'w', false, {0x308F, 0,      0,      0,      0x3092}},
    {'g', true,  {0x304C, 0x304E, 0x3050, 0x3052, 0x3054}},
    {'z', true,  {0x3056, 0x3058, 0x305A, 0x305C, 0x305E}},
    {'d', true,  {0x3060, 0x3062, 0x3065, 0x3067, 0x3069}},
    {'b', true,  {0x3070, 0x3073, 0x3076, 0x3079, 0x307C}},
    {'p', true,  {0x3071, 0x3074, 0x3077, 0x307A, 0x307D}},
};

static fmrb_kana_mode_t s_mode = FMRB_KANA_OFF;
static char s_pending = 0;    // consonant waiting for its vowel
static char s_pending2 = 0;   // second letter of "ky" / "sh" / "ch" / "ts"

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int vowel_index(char c)
{
    switch (c) {
        case 'a': return 0;
        case 'i': return 1;
        case 'u': return 2;
        case 'e': return 3;
        case 'o': return 4;
        default:  return -1;
    }
}

// J and F are the usual romaji spellings of the ZI and HU sounds (BASIC does
// the same substitution).
static char normalize_consonant(char c)
{
    if (c == 'j') return 'z';
    if (c == 'f') return 'h';
    return c;
}

static const kana_row_t *find_row(char consonant)
{
    for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); i++) {
        if (k_rows[i].consonant == consonant) {
            return &k_rows[i];
        }
    }
    return NULL;
}

// "c" has no row of its own: it only exists as the first half of "ch"/"cy".
// It still has to count as a consonant so that "chi" can start.
static bool is_consonant(char c)
{
    return c == 'c' || find_row(normalize_consonant(c)) != NULL;
}

static void emit_cp(fmrb_kana_result_t *r, uint16_t cp)
{
    if (cp == 0) {
        return;
    }
    if (s_mode == FMRB_KANA_KATAKANA && cp >= HIRA_FIRST && cp <= HIRA_LAST) {
        cp = (uint16_t)(cp + KATA_SHIFT);
    }
    if (r->out_len + 3 > (uint8_t)sizeof(r->out)) {
        return;
    }
    r->out[r->out_len++] = (uint8_t)(0xE0 | (cp >> 12));
    r->out[r->out_len++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    r->out[r->out_len++] = (uint8_t)(0x80 | (cp & 0x3F));
}

// Second letter of a two-letter consonant. Beyond the "y" of youon these are
// the Hepburn spellings a keyboard user reaches for even after learning the
// kunrei ones: shi, chi, tsu.
static bool extends_pending(char first, char second)
{
    if (second == 'y') {
        if (first == 'c') return true;  // cya = chya
        const kana_row_t *row = find_row(normalize_consonant(first));
        return row && row->youon;
    }
    if (second == 'h') return first == 's' || first == 'c';
    if (second == 's') return first == 't';   // only "tsu" resolves
    return false;
}

/**
 * Emit the kana for a finished romaji unit: first letter, optional second
 * letter, vowel index.
 */
static void resolve(char p1, char p2, int v, fmrb_kana_result_t *r)
{
    char row_c;
    bool youon = false;
    bool bare_i = false;   // "shi"/"chi"/"ji" are one kana, not a small-i pair

    if (p2 == 'y') {
        row_c = (p1 == 'c') ? 't' : normalize_consonant(p1);
        youon = true;
    } else if (p2 == 'h') {
        row_c = (p1 == 'c') ? 't' : 's';
        youon = true;
        bare_i = true;
    } else if (p2 == 's') {
        // "tsu" only; "tsa" and friends are not syllables.
        if (v == 2) {
            const kana_row_t *row = find_row('t');
            emit_cp(r, row->cp[2]);
        }
        return;
    } else if (p1 == 'j') {
        // "ja" is JI + small A, not the ZA row. "ji" stays one kana.
        row_c = 'z';
        youon = true;
        bare_i = true;
    } else if (p1 == 'c') {
        return;  // bare "ca": no such syllable
    } else {
        row_c = normalize_consonant(p1);
    }

    const kana_row_t *row = find_row(row_c);
    if (!row) return;
    if (!youon) {
        emit_cp(r, row->cp[v]);  // 0 for yi/ye/wi/wu/we: nothing comes out
        return;
    }
    if (!row->cp[1]) return;
    emit_cp(r, row->cp[1]);
    if (!(bare_i && v == 1)) {
        emit_cp(r, k_small[v]);
    }
}

static void clear_pending(void)
{
    s_pending = 0;
    s_pending2 = 0;
}

/**
 * Handle a key with no pending consonant in front of it.
 * Returns the action for the event itself.
 */
static fmrb_kana_action_t compose_fresh(char c, fmrb_kana_result_t *r)
{
    int v = vowel_index(c);
    if (v >= 0) {
        emit_cp(r, k_vowels[v]);
        return FMRB_KANA_COMPOSE;
    }
    if (is_consonant(c)) {
        s_pending = c;
        s_pending2 = 0;
        return FMRB_KANA_COMPOSE;
    }
    switch (c) {
        case ',': emit_cp(r, KANA_TOUTEN); return FMRB_KANA_COMPOSE;
        case '.': emit_cp(r, KANA_KUTEN);  return FMRB_KANA_COMPOSE;
        case '-': emit_cp(r, KANA_CHOON);  return FMRB_KANA_COMPOSE;
        default:  return FMRB_KANA_PASS;
    }
}

static fmrb_kana_action_t compose(char c, fmrb_kana_result_t *r)
{
    if (s_pending == 0) {
        return compose_fresh(c, r);
    }

    const char raw = s_pending;
    const char raw2 = s_pending2;
    const char cons = normalize_consonant(raw);
    const int v = vowel_index(c);

    if (raw2 != 0) {
        if (v >= 0) {
            clear_pending();
            resolve(raw, raw2, v, r);
            return FMRB_KANA_COMPOSE;
        }
        // Two letters and no vowel: the pair was not going anywhere. Drop it
        // and let this key start over.
        clear_pending();
        return compose(c, r);
    }

    if (cons == 'n' && c == 'n') {
        clear_pending();
        emit_cp(r, KANA_N);
        return FMRB_KANA_COMPOSE;
    }
    if (v >= 0) {
        clear_pending();
        resolve(raw, 0, v, r);
        return FMRB_KANA_COMPOSE;
    }
    if (extends_pending(raw, c)) {
        s_pending2 = c;
        return FMRB_KANA_COMPOSE;
    }
    if (c == raw && cons != 'n') {
        // Doubled consonant: commit the small tsu and keep waiting.
        emit_cp(r, KANA_SOKU);
        return FMRB_KANA_COMPOSE;
    }
    if (cons == 'n') {
        // "n" followed by anything else is the syllabic n. BASIC dropped it;
        // an editor has to keep it, or "kanji" loses its n.
        clear_pending();
        emit_cp(r, KANA_N);
        return compose_fresh(c, r);
    }
    // Any other pair is not a syllable. Drop the consonant and reconsider the
    // key, as BASIC does.
    clear_pending();
    return compose_fresh(c, r);
}

fmrb_kana_action_t fmrb_kana_feed(bool key_down, uint8_t scancode,
                                  uint8_t modifier, char ascii,
                                  fmrb_kana_result_t *result)
{
    memset(result, 0, sizeof(*result));

    const bool ctrl_alt = (modifier & (FMRB_KEYMAP_MOD_LCTRL | FMRB_KEYMAP_MOD_RCTRL |
                                       FMRB_KEYMAP_MOD_LALT | FMRB_KEYMAP_MOD_RALT)) != 0;

    // Ctrl+Space turns kana input on and off on any keyboard. Half/full-width
    // is a JIS key; a US keyboard does not have one, and neither does the
    // Tab5's built-in keyboard, so without this they could not reach kana
    // input at all. Tested before the Ctrl bypass further down, which is what
    // keeps Ctrl+letter shortcuts out of the composer.
    if (scancode == SC_SPACE && ctrl_alt) {
        if (key_down) {
            s_mode = (s_mode == FMRB_KANA_OFF) ? FMRB_KANA_HIRAGANA
                                               : FMRB_KANA_OFF;
            clear_pending();
            result->mode_changed = true;
        }
        return FMRB_KANA_CONSUME;
    }

    // Mode toggles. Half/full-width switches kana input on and off; the
    // katakana/hiragana key switches the script while it is on.
    //
    // Half/full-width is only taken over on the JP layout: on a US keyboard
    // that scancode is the backtick key and has to keep typing one.
    //
    // Half/full-width ignores every modifier on purpose. It used to switch
    // script when Shift was held, as a stand-in for keyboards with no
    // katakana key, and that made "off" unreachable: the katakana legend on a
    // JIS keyboard is the shifted one, so a user who reaches katakana with
    // Shift and keeps it held finds this key flipping hiragana and katakana
    // forever with no way out. The key that turns kana input off has to mean
    // that in every state, whatever else is pressed.
    if (scancode == SC_ZENKAKU && fmrb_keymap_get_layout() == FMRB_KEYMAP_LAYOUT_JP) {
        if (key_down) {
            s_mode = (s_mode == FMRB_KANA_OFF) ? FMRB_KANA_HIRAGANA
                                               : FMRB_KANA_OFF;
            clear_pending();
            result->mode_changed = true;
        }
        return FMRB_KANA_CONSUME;
    }
    if (scancode == SC_KANA && s_mode != FMRB_KANA_OFF) {
        if (key_down) {
            s_mode = (s_mode == FMRB_KANA_KATAKANA) ? FMRB_KANA_HIRAGANA
                                                    : FMRB_KANA_KATAKANA;
            clear_pending();
            result->mode_changed = true;
        }
        return FMRB_KANA_CONSUME;
    }

    if (s_mode == FMRB_KANA_OFF || ctrl_alt) {
        return FMRB_KANA_PASS;  // Ctrl+S and friends stay themselves
    }

    if (scancode == SC_ESCAPE) {
        clear_pending();
        return FMRB_KANA_PASS;
    }
    if (scancode == SC_BACKSPACE) {
        if (s_pending != 0) {
            if (key_down) {
                // Erase what the user typed but has not seen yet, not the
                // character before the cursor.
                if (s_pending2 != 0) {
                    s_pending2 = 0;
                } else {
                    clear_pending();
                }
            }
            return FMRB_KANA_CONSUME;
        }
        return FMRB_KANA_PASS;
    }

    const char c = lower(ascii);
    const bool composing = (vowel_index(c) >= 0) || is_consonant(c) ||
                           c == ',' || c == '.' || c == '-';

    if (!key_down) {
        // Releases never compose, but the release of a composing key must not
        // deliver its ASCII character either.
        return composing ? FMRB_KANA_COMPOSE : FMRB_KANA_PASS;
    }

    if (!composing && s_pending == 0) {
        return FMRB_KANA_PASS;
    }
    return compose(c, result);
}

fmrb_kana_mode_t fmrb_kana_get_mode(void)
{
    return s_mode;
}

void fmrb_kana_set_mode(fmrb_kana_mode_t mode)
{
    s_mode = mode;
    clear_pending();
}

void fmrb_kana_clear_pending(void)
{
    clear_pending();
}

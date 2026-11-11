/**
 * @file fmrb_kana.c
 * @brief Romaji to kana composition (see fmrb_kana.h).
 *
 * The conversion rules started from the BASIC interpreter's kana_translate
 * (components/basic/core/basic_screen.cpp) and grew the parts a text editor
 * needs: precomposed dakuten, youon, sokuon, the long vowel mark and the two
 * Japanese punctuation marks. Output is Unicode, not the BASIC screen's own
 * character codes.
 *
 * On top of those, the spellings the common IMEs (MS-IME, Google Japanese
 * Input) take for small kana and for the sounds of loanwords (k_rules,
 * doc/kanji_input K6): "xa" / "la" for a small a, "xtu" for a small tsu,
 * "thi" for "te" + small i, "fa" for "fu" + small a, "vu" for vu, and so on.
 * Everything not in that table keeps the meaning it had before: an x, l, v or
 * q that does not start one of them is typed as the letter.
 */

#include "fmrb_kana.h"
#include "fmrb_attr.h"
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

// The IME spellings (doc/kanji_input/report/k6.md has the table). Each one
// is complete as written; none is the start of another. Where a spelling
// also has a meaning in the rows above ("fa", "wi", "tsa"), this one wins.
typedef struct {
    char romaji[5];
    uint16_t cp[2];    // the kana, 0 in the second slot for one
} romaji_rule_t;

// Small a i u e o, small ya yu yo.
#define S_A 0x3041
#define S_I 0x3043
#define S_U 0x3045
#define S_E 0x3047
#define S_O 0x3049
#define S_YA 0x3083
#define S_YU 0x3085
#define S_YO 0x3087

static const romaji_rule_t k_rules[] = {
    // Small kana on their own: x or l in front.
    {"xa", {S_A}}, {"xi", {S_I}}, {"xu", {S_U}}, {"xe", {S_E}}, {"xo", {S_O}},
    {"la", {S_A}}, {"li", {S_I}}, {"lu", {S_U}}, {"le", {S_E}}, {"lo", {S_O}},
    {"xtu", {KANA_SOKU}}, {"xtsu", {KANA_SOKU}},
    {"ltu", {KANA_SOKU}}, {"ltsu", {KANA_SOKU}},
    {"xya", {S_YA}}, {"xyu", {S_YU}}, {"xyo", {S_YO}},
    {"lya", {S_YA}}, {"lyu", {S_YU}}, {"lyo", {S_YO}},
    {"xwa", {0x308E}}, {"lwa", {0x308E}},
    // Small ka and ke are katakana in both modes, as in the IMEs.
    {"xka", {0x30F5}}, {"xke", {0x30F6}}, {"lka", {0x30F5}}, {"lke", {0x30F6}},
    // te / de + small ya i yu e yo
    {"tha", {0x3066, S_YA}}, {"thi", {0x3066, S_I}}, {"thu", {0x3066, S_YU}},
    {"the", {0x3066, S_E}}, {"tho", {0x3066, S_YO}},
    {"dha", {0x3067, S_YA}}, {"dhi", {0x3067, S_I}}, {"dhu", {0x3067, S_YU}},
    {"dhe", {0x3067, S_E}}, {"dho", {0x3067, S_YO}},
    // to / do + small a i u e o
    {"twa", {0x3068, S_A}}, {"twi", {0x3068, S_I}}, {"twu", {0x3068, S_U}},
    {"twe", {0x3068, S_E}}, {"two", {0x3068, S_O}},
    {"dwa", {0x3069, S_A}}, {"dwi", {0x3069, S_I}}, {"dwu", {0x3069, S_U}},
    {"dwe", {0x3069, S_E}}, {"dwo", {0x3069, S_O}},
    // tsu + small a i e o
    {"tsa", {0x3064, S_A}}, {"tsi", {0x3064, S_I}}, {"tse", {0x3064, S_E}},
    {"tso", {0x3064, S_O}},
    // fu + small a i e o, small ya yu yo
    {"fa", {0x3075, S_A}}, {"fi", {0x3075, S_I}}, {"fe", {0x3075, S_E}},
    {"fo", {0x3075, S_O}},
    {"fya", {0x3075, S_YA}}, {"fyi", {0x3075, S_I}}, {"fyu", {0x3075, S_YU}},
    {"fye", {0x3075, S_E}}, {"fyo", {0x3075, S_YO}},
    // vu, and vu + small a i e o, small ya yu yo
    {"va", {0x3094, S_A}}, {"vi", {0x3094, S_I}}, {"vu", {0x3094}},
    {"ve", {0x3094, S_E}}, {"vo", {0x3094, S_O}},
    {"vya", {0x3094, S_YA}}, {"vyi", {0x3094, S_I}}, {"vyu", {0x3094, S_YU}},
    {"vye", {0x3094, S_E}}, {"vyo", {0x3094, S_YO}},
    // u + small a i e o
    {"wi", {0x3046, S_I}}, {"we", {0x3046, S_E}},
    {"wha", {0x3046, S_A}}, {"whi", {0x3046, S_I}}, {"whu", {0x3046}},
    {"whe", {0x3046, S_E}}, {"who", {0x3046, S_O}},
    // ku / gu + small a i u e o, and q for ku
    {"kwa", {0x304F, S_A}}, {"kwi", {0x304F, S_I}}, {"kwu", {0x304F, S_U}},
    {"kwe", {0x304F, S_E}}, {"kwo", {0x304F, S_O}},
    {"gwa", {0x3050, S_A}}, {"gwi", {0x3050, S_I}}, {"gwu", {0x3050, S_U}},
    {"gwe", {0x3050, S_E}}, {"gwo", {0x3050, S_O}},
    {"qa", {0x304F, S_A}}, {"qi", {0x304F, S_I}}, {"qu", {0x304F}},
    {"qe", {0x304F, S_E}}, {"qo", {0x304F, S_O}},
};

// Longest romaji a rule has ("xtsu"); the pending part is one shorter.
#define ROMAJI_MAX 4

static fmrb_kana_mode_t s_mode = FMRB_KANA_OFF;
// Romaji typed but not yet a kana, as typed (case kept: an x that turns out
// to be a letter is typed as it was). PSRAM: the internal RAM budget has no
// room for new statics.
FMRB_EXT_RAM_BSS_ATTR static char s_buf[ROMAJI_MAX - 1];
FMRB_EXT_RAM_BSS_ATTR static uint8_t s_len;

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

// Letters that only start an IME spelling (k_rules). When what follows does
// not make one, the letter is typed as itself, as it always was.
static bool is_letter_start(char c)
{
    return c == 'x' || c == 'l' || c == 'v' || c == 'q';
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

static void emit_letter(fmrb_kana_result_t *r, char c)
{
    if (r->out_len < (uint8_t)sizeof(r->out)) {
        r->out[r->out_len++] = (uint8_t)c;
    }
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
    if (second == 's') return first == 't';
    return false;
}

/**
 * Emit the kana for a finished romaji unit of the rows: first letter,
 * optional second letter, vowel index.
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
        // "tsu"; the rest of the ts row is in k_rules.
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
        emit_cp(r, row->cp[v]);  // 0 for yi/ye/wu: nothing comes out
        return;
    }
    if (!row->cp[1]) return;
    emit_cp(r, row->cp[1]);
    if (!(bare_i && v == 1)) {
        emit_cp(r, k_small[v]);
    }
}

typedef enum {
    MATCH_NONE = 0,   // not the start of anything
    MATCH_PREFIX,     // more letters may make something
    MATCH_RULE,       // a k_rules spelling, complete
    MATCH_ROW,        // a spelling of the rows, complete (resolve)
} match_t;

/** What the lower-case romaji s[0..n) is. *rule is set for MATCH_RULE. */
static match_t match(const char *s, size_t n, const romaji_rule_t **rule)
{
    bool prefix = false;
    for (size_t i = 0; i < sizeof(k_rules) / sizeof(k_rules[0]); i++) {
        const char *k = k_rules[i].romaji;
        if (strncmp(k, s, n) == 0) {
            if (k[n] == '\0') {
                *rule = &k_rules[i];
                return MATCH_RULE;
            }
            prefix = true;
        }
    }
    if (n == 2 && is_consonant(s[0]) && vowel_index(s[1]) >= 0) {
        return MATCH_ROW;
    }
    if (n == 3 && extends_pending(s[0], s[1]) && vowel_index(s[2]) >= 0) {
        return MATCH_ROW;
    }
    if (n == 2 && extends_pending(s[0], s[1])) {
        prefix = true;
    }
    return prefix ? MATCH_PREFIX : MATCH_NONE;
}

static void clear_pending(void)
{
    s_len = 0;
}

// An x / l / v / q that started the pending romaji, typed as the letter it
// is (the rest is dropped). For Esc and the mode keys, which drop what is
// pending: before the IME spellings the letter was already on the screen.
static void flush_letter(fmrb_kana_result_t *r)
{
    if (s_len > 0 && is_letter_start(lower(s_buf[0]))) {
        emit_letter(r, s_buf[0]);
    }
    clear_pending();
}

static fmrb_kana_action_t compose(char raw, fmrb_kana_result_t *r);

/**
 * Handle a key with no pending romaji in front of it.
 * Returns the action for the event itself.
 */
static fmrb_kana_action_t compose_fresh(char raw, fmrb_kana_result_t *r)
{
    const char c = lower(raw);
    int v = vowel_index(c);
    if (v >= 0) {
        emit_cp(r, k_vowels[v]);
        return FMRB_KANA_COMPOSE;
    }
    if (is_consonant(c) || is_letter_start(c)) {
        s_buf[0] = raw;
        s_len = 1;
        return FMRB_KANA_COMPOSE;
    }
    switch (c) {
        case ',': emit_cp(r, KANA_TOUTEN); return FMRB_KANA_COMPOSE;
        case '.': emit_cp(r, KANA_KUTEN);  return FMRB_KANA_COMPOSE;
        case '-': emit_cp(r, KANA_CHOON);  return FMRB_KANA_COMPOSE;
        default:  return FMRB_KANA_PASS;
    }
}

// Drop the first `drop` pending letters and read the rest and this key again.
static fmrb_kana_action_t reread(size_t drop, char raw, fmrb_kana_result_t *r)
{
    char rest[ROMAJI_MAX];
    size_t n = s_len - drop;
    memcpy(rest, s_buf + drop, n);
    clear_pending();
    for (size_t i = 0; i < n; i++) {
        (void)compose(rest[i], r);
    }
    return compose(raw, r);
}

static fmrb_kana_action_t compose(char raw, fmrb_kana_result_t *r)
{
    if (s_len == 0) {
        return compose_fresh(raw, r);
    }

    char seq[ROMAJI_MAX];
    for (size_t i = 0; i < s_len; i++) {
        seq[i] = lower(s_buf[i]);
    }
    const char c = lower(raw);
    seq[s_len] = c;
    const size_t n = (size_t)s_len + 1;
    const char first = seq[0];

    if (n == 2 && first == 'n' && c == 'n') {
        clear_pending();
        emit_cp(r, KANA_N);
        return FMRB_KANA_COMPOSE;
    }

    const romaji_rule_t *rule = NULL;
    switch (match(seq, n, &rule)) {
        case MATCH_RULE:
            clear_pending();
            emit_cp(r, rule->cp[0]);
            emit_cp(r, rule->cp[1]);
            return FMRB_KANA_COMPOSE;
        case MATCH_ROW:
            clear_pending();
            if (n == 3) {
                resolve(seq[0], seq[1], vowel_index(seq[2]), r);
            } else {
                resolve(seq[0], 0, vowel_index(seq[1]), r);
            }
            return FMRB_KANA_COMPOSE;
        case MATCH_PREFIX:
            s_buf[s_len++] = raw;
            return FMRB_KANA_COMPOSE;
        case MATCH_NONE:
        default:
            break;
    }

    // Not going anywhere. What happens is what happened before the IME
    // spellings existed.
    if (is_letter_start(first)) {
        // The letter is typed as itself; the rest is read again.
        emit_letter(r, s_buf[0]);
        return reread(1, raw, r);
    }
    if (s_len == 1) {
        if (c == first && first != 'n') {
            // Doubled consonant: commit the small tsu and keep waiting.
            emit_cp(r, KANA_SOKU);
            return FMRB_KANA_COMPOSE;
        }
        clear_pending();
        if (first == 'n') {
            // "n" followed by anything else is the syllabic n. BASIC dropped
            // it; an editor has to keep it, or "kanji" loses its n.
            emit_cp(r, KANA_N);
        }
        // Any other pair is not a syllable. Drop the consonant and reconsider
        // the key, as BASIC does.
        return compose_fresh(raw, r);
    }
    if (extends_pending(seq[0], seq[1])) {
        // Two letters of the rows ("ky", "sh", "ts") and no vowel: the pair
        // was not going anywhere. Drop it and let this key start over.
        clear_pending();
        return compose_fresh(raw, r);
    }
    // The start of an IME spelling that did not come ("th", "kw"): before,
    // the first letter was dropped when the second came.
    return reread(1, raw, r);
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
            flush_letter(result);
            s_mode = (s_mode == FMRB_KANA_OFF) ? FMRB_KANA_HIRAGANA
                                               : FMRB_KANA_OFF;
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
            flush_letter(result);
            s_mode = (s_mode == FMRB_KANA_OFF) ? FMRB_KANA_HIRAGANA
                                               : FMRB_KANA_OFF;
            result->mode_changed = true;
        }
        return FMRB_KANA_CONSUME;
    }
    if (scancode == SC_KANA && s_mode != FMRB_KANA_OFF) {
        if (key_down) {
            flush_letter(result);
            s_mode = (s_mode == FMRB_KANA_KATAKANA) ? FMRB_KANA_HIRAGANA
                                                    : FMRB_KANA_KATAKANA;
            result->mode_changed = true;
        }
        return FMRB_KANA_CONSUME;
    }

    if (s_mode == FMRB_KANA_OFF || ctrl_alt) {
        return FMRB_KANA_PASS;  // Ctrl+S and friends stay themselves
    }

    if (scancode == SC_ESCAPE) {
        if (key_down) {
            flush_letter(result);
        }
        return FMRB_KANA_PASS;
    }
    if (scancode == SC_BACKSPACE) {
        if (s_len != 0) {
            if (key_down) {
                // Erase what the user typed but has not seen yet, not the
                // character before the cursor. The start of an IME
                // spelling of two consonants ("th") goes whole: before, its
                // first letter was already gone when the second came.
                const char a = lower(s_buf[0]);
                const char b = (s_len > 1) ? lower(s_buf[1]) : 0;
                if (s_len == 2 && !is_letter_start(a) && !extends_pending(a, b)) {
                    clear_pending();
                } else {
                    s_len--;
                }
            }
            return FMRB_KANA_CONSUME;
        }
        return FMRB_KANA_PASS;
    }

    const char c = lower(ascii);
    const bool composing = (vowel_index(c) >= 0) || is_consonant(c) ||
                           is_letter_start(c) ||
                           c == ',' || c == '.' || c == '-';

    if (!key_down) {
        // Releases never compose, but the release of a composing key must not
        // deliver its ASCII character either.
        return composing ? FMRB_KANA_COMPOSE : FMRB_KANA_PASS;
    }

    if (!composing && s_len == 0) {
        return FMRB_KANA_PASS;
    }
    return compose(ascii, result);
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

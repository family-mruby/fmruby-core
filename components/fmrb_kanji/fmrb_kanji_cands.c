// The candidate list a conversion shows (doc/kanji_input K3, K6). See
// fmrb_kanji_cands.h.

#include "fmrb_kanji_cands.h"

#include <string.h>

// Katakana to hiragana, in place: a reading typed in katakana mode means the
// same word. Both are three-byte UTF-8 (U+30A1-U+30F6 -> U+3041-U+3096), so
// the byte length is kept and the matched length still counts the reading as
// it was typed.
static void fold_katakana(char *s, size_t len)
{
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i + 2 < len; ) {
        if (p[i] < 0x80) { i++; continue; }
        if (p[i] == 0xE3 && (p[i + 1] == 0x82 || p[i + 1] == 0x83)) {
            uint32_t cp = ((uint32_t)(p[i] & 0x0F) << 12) |
                          ((uint32_t)(p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            if (cp >= 0x30A1 && cp <= 0x30F6) {
                cp -= 0x60;
                p[i + 1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                p[i + 2] = (uint8_t)(0x80 | (cp & 0x3F));
            }
            i += 3;
            continue;
        }
        // Skip the rest of any other character.
        i++;
        while (i < len && (p[i] & 0xC0) == 0x80) i++;
    }
}

// Hiragana to katakana, from src into dst (same byte length: both are
// three-byte UTF-8). Anything else (the long vowel mark, punctuation) is
// copied as it is.
static void to_katakana(const char *src, size_t len, char *dst)
{
    const uint8_t *p = (const uint8_t *)src;
    uint8_t *q = (uint8_t *)dst;
    memcpy(q, p, len);
    for (size_t i = 0; i + 2 < len; ) {
        if (p[i] == 0xE3 && (p[i + 1] == 0x81 || p[i + 1] == 0x82)) {
            uint32_t cp = ((uint32_t)(p[i] & 0x0F) << 12) |
                          ((uint32_t)(p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096) {
                cp += 0x60;
                q[i + 1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                q[i + 2] = (uint8_t)(0x80 | (cp & 0x3F));
            }
            i += 3;
            continue;
        }
        i++;
        while (i < len && (p[i] & 0xC0) == 0x80) i++;
    }
}

static void reverse(char *s, size_t n)
{
    for (size_t i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
}

// The katakana of reading[0, bytes) as candidate `at`: made at the end of the
// text and rotated into place (no extra room). With `dedupe`, not added when
// one of the first n candidates is the same text.
static bool insert_katakana(const fmrb_kanji_cand_buf_t *buf, const char *reading,
                            uint16_t bytes, uint16_t *n, uint16_t at, bool dedupe)
{
    char *text = buf->text;
    uint16_t *offset = buf->offset;
    uint16_t pos = offset[*n];
    if (*n >= buf->max_cands || (size_t)pos + bytes > buf->text_size) {
        return false;
    }
    to_katakana(reading, bytes, &text[pos]);
    if (dedupe) {
        for (uint16_t i = 0; i < *n; i++) {
            if (offset[i + 1] - offset[i] == bytes &&
                memcmp(&text[offset[i]], &text[pos], bytes) == 0) {
                return false;
            }
        }
    }
    // [offset[at], pos) then itself becomes itself then [offset[at], pos).
    uint16_t start = offset[at];
    reverse(&text[start], (size_t)(pos - start));
    reverse(&text[pos], bytes);
    reverse(&text[start], (size_t)(pos + bytes - start));
    for (uint16_t i = (uint16_t)(*n + 1); i > at; i--) {
        offset[i] = (uint16_t)(offset[i - 1] + bytes);
    }
    offset[at] = start;
    (*n)++;
    return true;
}

fmrb_err_t fmrb_kanji_make_candidates(fmrb_kanji_dict_t *dict, char *reading, size_t len,
                                      uint8_t *block, const fmrb_kanji_cand_buf_t *buf,
                                      fmrb_kanji_cands_t *out)
{
    fold_katakana(reading, len);
    out->found = false;
    out->matched_bytes = 0;
    out->count = 0;
    out->whole_index = FMRB_KANJI_NO_INDEX;
    buf->offset[0] = 0;

    fmrb_kanji_result_t kr;
    fmrb_err_t err = fmrb_kanji_longest_prefix(dict, reading, len, block, &kr);
    if (err != FMRB_OK && err != FMRB_ERR_NOT_FOUND) {
        return err;
    }
    uint16_t matched = (err == FMRB_OK) ? kr.matched_bytes : (uint16_t)len;
    bool partial = err == FMRB_OK && matched < len;
    // Room kept for the katakana forms: one or two candidates.
    uint16_t kata_slots = partial ? 2 : 1;
    size_t kata_bytes = (size_t)matched + (partial ? len : 0);

    uint16_t n = 0;
    uint16_t pos = 0;
    if (err == FMRB_OK) {
        // Each candidate straight into the text; FMRB_KANJI_CAND_UTF8_MAX of
        // room left guarantees the next one fits.
        while (n < kr.count && n + kata_slots < buf->max_cands &&
               (size_t)pos + FMRB_KANJI_CAND_UTF8_MAX + kata_bytes <= buf->text_size) {
            size_t clen = 0;
            if (fmrb_kanji_candidate(&kr, n, &buf->text[pos], buf->text_size - pos,
                                     &clen) != FMRB_OK) {
                break;
            }
            buf->offset[n] = pos;
            pos = (uint16_t)(pos + clen);
            n++;
        }
        buf->offset[n] = pos;
    }
    // The dictionary has no katakana spelled exactly as its reading, so these
    // make any reading katakana in one conversion. Second, after the
    // dictionary's first (first when it has none); the whole reading in front
    // of the matched part.
    uint16_t at = (n > 0) ? 1 : 0;
    insert_katakana(buf, reading, matched, &n, at, true);
    if (partial && insert_katakana(buf, reading, (uint16_t)len, &n, at, false)) {
        out->whole_index = at;
    }
    out->found = n > 0;
    out->matched_bytes = out->found ? matched : 0;
    out->count = n;
    return err;
}

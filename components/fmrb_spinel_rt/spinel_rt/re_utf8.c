/*
** re_utf8.c - UTF-8 utility functions for regexp engine
**
** See Copyright Notice in mruby.h
*/

#include "re_internal.h"

/* Return byte length of UTF-8 character at s.
   Returns 1 for invalid sequences (treat as single byte). */
int
re_utf8_charlen(const char *s, const char *end)
{
  uint8_t c = (uint8_t)*s;
  int len;

  if (c < 0x80) return 1;
  else if (c < 0xc0) return 1;  /* invalid continuation */
  else if (c < 0xe0) len = 2;
  else if (c < 0xf0) len = 3;
  else if (c < 0xf8) len = 4;
  else return 1;  /* invalid */

  if (s + len > end) return 1;  /* truncated */
  return len;
}

/* Decode a UTF-8 character and return its codepoint.
   *len is set to the byte length consumed.
   The buffer end is checked before any continuation byte is read
   (imported from mruby): a truncated multi-byte leader at the end of
   a pattern or subject consumes a single byte instead of reading past
   the buffer, mirroring re_utf8_charlen.
   Issue #780: reject overlong sequences (where a multi-byte form
   encodes a codepoint that fits in fewer bytes). RFC 3629 + Ruby
   both treat these as invalid. Returns U+FFFD (replacement) and
   consumes just the lead byte so the caller can resync. */
uint32_t
re_utf8_decode(const char *s, const char *end, int *len)
{
  uint8_t c = (uint8_t)s[0];
  uint32_t cp;

  if (c < 0x80) {
    *len = 1;
    return c;
  }
  else if (c < 0xc0) {
    *len = 1;
    return c;  /* invalid lead byte, return as-is */
  }
  else if (c < 0xe0) {
    /* Issue #754: validate continuation byte before reading further.
       Rejects any byte without the 10xxxxxx continuation pattern. */
    if (s + 2 > end || ((uint8_t)s[1] & 0xc0) != 0x80) { *len = 1; return 0xFFFD; }
    *len = 2;
    cp = (c & 0x1f) << 6;
    cp |= ((uint8_t)s[1] & 0x3f);
    if (cp < 0x80) { *len = 1; return 0xFFFD; }
    return cp;
  }
  else if (c < 0xf0) {
    if (s + 3 > end ||
        ((uint8_t)s[1] & 0xc0) != 0x80 || ((uint8_t)s[2] & 0xc0) != 0x80) {
      *len = 1; return 0xFFFD;
    }
    *len = 3;
    cp = (c & 0x0f) << 12;
    cp |= ((uint8_t)s[1] & 0x3f) << 6;
    cp |= ((uint8_t)s[2] & 0x3f);
    if (cp < 0x800) { *len = 1; return 0xFFFD; }
    return cp;
  }
  else {
    if (s + 4 > end ||
        ((uint8_t)s[1] & 0xc0) != 0x80 || ((uint8_t)s[2] & 0xc0) != 0x80 ||
        ((uint8_t)s[3] & 0xc0) != 0x80) {
      *len = 1; return 0xFFFD;
    }
    *len = 4;
    cp = (c & 0x07) << 18;
    cp |= ((uint8_t)s[1] & 0x3f) << 12;
    cp |= ((uint8_t)s[2] & 0x3f) << 6;
    cp |= ((uint8_t)s[3] & 0x3f);
    if (cp < 0x10000 || cp > 0x10FFFF) { *len = 1; return 0xFFFD; }
    return cp;
  }
}

/* ---- character types above ASCII (see re_internal.h) ---- */
#ifdef RE_UNICODE_CTYPE
#include "re_ctype.h"

/* The table numbers its bits in the order re_internal.h names them, so that
   an entry read off it is a re_ctype value as it stands. */
_Static_assert(RE_CTYPE_TABLE_ALPHA == RE_CTYPE_ALPHA &&
               RE_CTYPE_TABLE_UPPER == RE_CTYPE_UPPER &&
               RE_CTYPE_TABLE_LOWER == RE_CTYPE_LOWER &&
               RE_CTYPE_TABLE_DIGIT == RE_CTYPE_DIGIT &&
               RE_CTYPE_TABLE_ALNUM == RE_CTYPE_ALNUM &&
               RE_CTYPE_TABLE_WORD  == RE_CTYPE_WORD &&
               RE_CTYPE_TABLE_PUNCT == RE_CTYPE_PUNCT &&
               RE_CTYPE_TABLE_SPACE == RE_CTYPE_SPACE &&
               RE_CTYPE_TABLE_BLANK == RE_CTYPE_BLANK &&
               RE_CTYPE_TABLE_GRAPH == RE_CTYPE_GRAPH &&
               RE_CTYPE_TABLE_PRINT == RE_CTYPE_PRINT &&
               RE_CTYPE_CNTRL >= (1 << RE_CTYPE_MASK_BITS),
               "re_ctype.h and re_internal.h number the types differently");

/* The types of a codepoint above ASCII: the set of the run it falls in, which
   is the last run starting at or below it, and cntrl from its range. */
uint16_t
re_ctype(uint32_t cp)
{
  if (cp < RE_CTYPE_MIN) return 0;
  size_t lo = 0, hi = RE_CTYPE_RUN_COUNT;
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if ((re_ctype_runs[mid] >> RE_CTYPE_MASK_BITS) <= cp) lo = mid;
    else hi = mid;
  }
  uint16_t t = (uint16_t)(re_ctype_runs[lo] & ((1u << RE_CTYPE_MASK_BITS) - 1));
  if (cp >= RE_CTYPE_CNTRL_LO && cp <= RE_CTYPE_CNTRL_HI) t |= RE_CTYPE_CNTRL;
  return t;
}

/* Whether a class holds a codepoint above ASCII through the POSIX brackets in
   it, once its ranges have said nothing: yes when the codepoint's type has a
   bit of ctype_yes, or lacks a bit of ctype_no, and failing both whatever
   utf8_any says.

   Under /i a character is in the class when any character sharing its folding
   is, so the question is put to every one of them: a positive bracket wants a
   type any of them has, a negated one a type any of them lacks. The ASCII
   ones are left out, since what the class holds through an ASCII counterpart
   is in its ranges already; see compile_charclass(). */
mrb_bool
re_class_ctype_match(const re_charclass *cc, uint32_t cp)
{
  uint16_t any = re_ctype(cp), all = any;
  if (cc->ctype_fold) {
    uint32_t alt[RE_CASE_ALTS_MAX];
    int n = re_case_alts(cp, alt);
    for (int i = 0; i < n; i++) {
      if (alt[i] < 128) continue;
      uint16_t t = re_ctype(alt[i]);
      any |= t;
      all &= t;
    }
  }
  return (any & cc->ctype_yes) || (~all & cc->ctype_no) || cc->utf8_any;
}

/* ---- Unicode properties (\p{...}) ---- */
#include "re_uniprop.h"

/* The general category of a codepoint: the category of the run it falls in,
   which is the last run starting at or below it. ASCII is in the table, so no
   floor test -- unlike the ctype table, which starts at U+0080. */
static uint32_t
re_general_category(uint32_t cp)
{
  size_t lo = 0, hi = RE_GC_RUNS;
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (RE_GC_RUN_START(re_gc_table[mid]) <= cp) lo = mid;
    else hi = mid;
  }
  return RE_GC_RUN_CAT(re_gc_table[lo]);
}

static uint32_t
re_emoji_set(uint32_t cp)
{
  if (cp < RE_EMOJI_RUN_START(re_emoji_table[0])) return 0;
  size_t lo = 0, hi = RE_EMOJI_RUNS;
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2;
    if (RE_EMOJI_RUN_START(re_emoji_table[mid]) <= cp) lo = mid;
    else hi = mid;
  }
  return RE_EMOJI_RUN_SET(re_emoji_table[lo]);
}

/* Resolve a `\p{...}` name to a property id, or -1. Case- and separator-
   insensitive, as CRuby is: `\p{Extended_Pictographic}`, `\p{extended
   pictographic}` and `\p{EXTENDEDPICTOGRAPHIC}` are one name. The POSIX
   names resolve elsewhere (to a ctype bit), so they are not here. */
static char
re_prop_lc(char ch)
{
  return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
}

int
re_prop_lookup(const char *name, size_t len)
{
  char norm[64];
  size_t n = 0;
  for (size_t i = 0; i < len && n + 1 < sizeof(norm); i++) {
    char ch = name[i];
    if (ch == '_' || ch == '-' || ch == ' ') continue;
    norm[n++] = re_prop_lc(ch);
  }
  norm[n] = 0;
  if (n == 0) return -1;
  /* A one-letter category (L, N, P, ...) stands for every two-letter one
     beginning with it, which the matcher answers by comparing first letters.
     Encoded as RE_PROP_MAJOR + the letter. */
  if (n == 1) {
    for (int i = 0; i < RE_GC_COUNT; i++)
      if (re_prop_lc(re_gc_names[i][0]) == norm[0]) return RE_PROP_MAJOR + norm[0];
    return -1;
  }
  if (n == 2) {
    for (int i = 0; i < RE_GC_COUNT; i++) {
      const char *g = re_gc_names[i];
      if (re_prop_lc(g[0]) == norm[0] && re_prop_lc(g[1]) == norm[1])
        return RE_PROP_GC + i;
    }
  }
  static const struct { const char *name; uint32_t bit; } emo[] = {
    { "emoji", RE_EMOJI_EMOJI },
    { "emojipresentation", RE_EMOJI_EMOJI_PRESENTATION },
    { "extendedpictographic", RE_EMOJI_EXTENDED_PICTOGRAPHIC },
  };
  for (size_t i = 0; i < sizeof(emo) / sizeof(emo[0]); i++)
    if (strcmp(norm, emo[i].name) == 0) return RE_PROP_EMOJI + (int)emo[i].bit;
  return -1;
}

/* Does `cp` have the property `id` (as returned by re_prop_lookup)? */
mrb_bool
re_prop_match(int id, uint32_t cp)
{
  if (id >= RE_PROP_EMOJI)
    return (re_emoji_set(cp) & (uint32_t)(id - RE_PROP_EMOJI)) != 0 ? TRUE : FALSE;
  if (id >= RE_PROP_MAJOR) {
    char want = (char)(id - RE_PROP_MAJOR);
    return (re_prop_lc(re_gc_names[re_general_category(cp)][0]) == want) ? TRUE : FALSE;
  }
  if (id >= RE_PROP_GC)
    return (re_general_category(cp) == (uint32_t)(id - RE_PROP_GC)) ? TRUE : FALSE;
  return FALSE;
}

/* Whether a class holds `cp` through the properties named in it: the same
   yes/no pair the POSIX brackets use, one bit per property the class names.
   Properties are not folded under /i -- a category IS the case distinction
   (`\p{Lu}` asks for an uppercase letter), so closing it under folding would
   answer the opposite of what was asked. */
mrb_bool
re_class_prop_match(const re_charclass *cc, uint32_t cp)
{
  for (int i = 0; i < cc->num_props; i++)
    if (re_prop_match(cc->props[i].id, cp) == (cc->props[i].negated ? FALSE : TRUE))
      return TRUE;
  return FALSE;
}
#endif  /* RE_UNICODE_CTYPE */

/* Check if character is a "word" character (\w): [a-zA-Z0-9_] */
mrb_bool
re_is_word_char(uint32_t c)
{
  if (c >= 'a' && c <= 'z') return TRUE;
  if (c >= 'A' && c <= 'Z') return TRUE;
  if (c >= '0' && c <= '9') return TRUE;
  if (c == '_') return TRUE;
  return FALSE;
}

/* Encode a codepoint as UTF-8 into buf and return the byte length, at most 4.
   Callers reject a surrogate and anything above U+10FFFF before they get
   here, so every input has an encoding. (ported from mruby-regexp 048e5da5f) */
int
re_utf8_encode(uint32_t cp, char *buf)
{
  if (cp < 0x80) {
    buf[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    buf[0] = (char)(0xc0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp < 0x10000) {
    buf[0] = (char)(0xe0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    buf[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  buf[0] = (char)(0xf0 | (cp >> 18));
  buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
  buf[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

/* ---- simple case folding (see re_internal.h) ---- */
#ifdef RE_UNICODE_CASE
#include "re_casefold.h"

static mrb_bool
fold_run_holds(const re_fold_run *r, uint32_t cp)
{
  return cp >= r->lo && cp <= r->hi && ((cp - r->lo) % r->stride) == 0;
}

uint32_t
re_case_fold(uint32_t cp)
{
  for (int i = 0; i < RE_FOLD_RUN_COUNT; i++) {
    const re_fold_run *r = &re_fold_runs[i];
    if (cp < r->lo) break;               /* runs are sorted by lo */
    if (fold_run_holds(r, cp)) return (uint32_t)((int32_t)cp + r->delta);
  }
  return cp;
}

int
re_case_alts(uint32_t cp, uint32_t *out)
{
  uint32_t f = re_case_fold(cp);
  int n = 0;
  out[n++] = f;
  /* every source that folds to f is a counterpart; the walk is over runs
     rather than codepoints, so a wide range costs the run count */
  for (int i = 0; i < RE_FOLD_RUN_COUNT && n < RE_CASE_ALTS_MAX; i++) {
    const re_fold_run *r = &re_fold_runs[i];
    int64_t src = (int64_t)f - r->delta;
    if (src < 0) continue;
    if (!fold_run_holds(r, (uint32_t)src)) continue;
    if ((uint32_t)src == f) continue;
    out[n++] = (uint32_t)src;
  }
  return n;
}
#else
uint32_t
re_case_fold(uint32_t cp)
{
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  return cp;
}

int
re_case_alts(uint32_t cp, uint32_t *out)
{
  uint32_t f = re_case_fold(cp);
  out[0] = f;
  if (f >= 'a' && f <= 'z') { out[1] = f - 32; return 2; }
  return 1;
}
#endif  /* RE_UNICODE_CASE */

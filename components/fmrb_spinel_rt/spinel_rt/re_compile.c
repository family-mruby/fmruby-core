/*
** re_compile.c - regexp pattern compiler
**
** Compiles a regular expression pattern string into bytecode
** for the NFA execution engine.
**
** See Copyright Notice in mruby.h
*/

#include "re_internal.h"
/* mruby header removed */
#include <string.h>

/* Compiler state */
typedef struct {
  const char *src;     /* pattern source */
  const char *src_end;
  const char *p;       /* current position */
  re_inst *code;       /* instruction array */
  uint32_t code_len;
  uint32_t code_capa;
  re_charclass *classes;
  uint16_t num_classes;
  uint16_t class_capa;
  uint16_t num_captures;
  uint32_t flags;
  re_named_capture *named_captures;
  uint16_t num_named;
  mrb_bool has_backref;
  mrb_bool needs_backtrack;
  char *stripped;           /* allocated buffer for x-mode preprocessing */
} re_compiler;

static void compile_alt(re_compiler *c);  /* forward */

/* Issue #781: error handler hook so the library can route through
   the user program's sp_raise_cls (which is `static inline` per
   translation unit and not directly linkable from a .a). The user
   program calls sp_re_set_error_handler(fn) at startup; fn should
   not return (typically wraps sp_raise_cls). If unset, fall back
   to fprintf + exit. */
static void (*sp_re_error_handler)(const char *msg) = NULL;
void sp_re_set_error_handler(void (*fn)(const char *msg)) {
  sp_re_error_handler = fn;
}

static __attribute__((noreturn)) void
compile_error(re_compiler *c, const char *msg)
{
  /* Build the message before freeing: in /x (extended) mode c->src aliases
     c->stripped, so reading it after the free would be a use-after-free. */
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s: /%.*s/",
           msg, (int)(c->src_end - c->src), c->src);
  /* Free all half-built compiler state: a catchable RegexpError handler
     longjmps out and `c` never returns to re_compile, so the bytecode,
     char-class ranges, and named-capture names would otherwise leak. (On
     success these transfer to the compiled pattern instead.) */
  free(c->code);
  c->code = NULL;
  if (c->classes) {
    for (uint16_t i = 0; i < c->num_classes; i++) free(c->classes[i].ranges);
    free(c->classes);
    c->classes = NULL;
  }
  if (c->named_captures) {
    for (uint16_t i = 0; i < c->num_named; i++) free((void *)c->named_captures[i].name);
    free(c->named_captures);
    c->named_captures = NULL;
  }
  if (c->stripped) free(c->stripped);
  c->stripped = NULL;
  if (sp_re_error_handler) {
    sp_re_error_handler(buf);
    /* shouldn't return; fall through to exit as a safety net */
  }
  fprintf(stderr, "RegexpError: %s\n", buf);
  exit(1);
}

static uint32_t
emit(re_compiler *c, uint8_t op, uint8_t a, uint16_t offset)
{
  if (c->code_len >= c->code_capa) {
    /* Issue #821: detect uint32_t overflow on doubling. The previous
       form silently wrapped to a small value (e.g. 2^31 doubles to
       0), realloc'd a tiny buffer, then wrote past it. Cap at
       (UINT32_MAX / 2) before doubling so the next * 2 stays in
       range; if we're already past that, raise instead of overflowing. */
    if (c->code_capa > (uint32_t)0x40000000u) {
      compile_error(c, "regexp too large");
    }
    uint32_t new_capa = c->code_capa ? c->code_capa * 2 : 64;
    re_inst *nc = (re_inst*)realloc(c->code, sizeof(re_inst) * new_capa);
    if (!nc) compile_error(c, "regexp too large");
    c->code = nc;
    c->code_capa = new_capa;
  }
  uint32_t pos = c->code_len++;
  c->code[pos].op = op;
  c->code[pos].a = a;
  c->code[pos].offset = offset;
  return pos;
}

static void
patch(re_compiler *c, uint32_t pos, uint16_t offset)
{
  c->code[pos].offset = offset;
}

/* Insert an instruction at position `pos` by shifting code.
   Adjusts all jump offsets >= pos by +1. */
static void
insert_inst(re_compiler *c, uint32_t pos, uint8_t op, uint8_t a, uint16_t offset)
{
  emit(c, RE_JMP, 0, 0);  /* grow array */
  uint32_t len = c->code_len - 1 - pos;
  memmove(&c->code[pos + 1], &c->code[pos], sizeof(re_inst) * len);
  c->code[pos].op = op;
  c->code[pos].a = a;
  c->code[pos].offset = offset;

  /* Fix jump targets across the insertion. A target past `pos` shifts down by
     one. A target equal to `pos` is ambiguous (mruby da41af3c9):
     - code that moved (i > pos) is a backward jump -- e.g. the SPLIT that
       loops `\d+` back to its class -- and meant the instruction now at
       pos+1, so it must follow it.
     - code before the insertion (i < pos) is a forward "skip to here"
       reference (e.g. a quantifier's skip-past-atom jump, or a lookaround's
       jump-to-end target) that should stay on the newly inserted instruction.
     Issue #824: LOOKAHEAD/NEG_LOOKAHEAD/LOOKBEHIND/NEG_LOOKBEHIND carry their
     jump-to-end target in `offset` too. */
  for (uint32_t i = 0; i < c->code_len; i++) {
    if (i == pos) continue;
    switch (c->code[i].op) {
    case RE_JMP: case RE_SPLIT: case RE_SPLITNG:
    case RE_LOOKAHEAD: case RE_NEG_LOOKAHEAD:
    case RE_LOOKBEHIND: case RE_NEG_LOOKBEHIND:
      if (c->code[i].offset >= 0xffff) break;
      if (c->code[i].offset > pos || (c->code[i].offset == pos && i > pos)) {
        c->code[i].offset++;
      }
      break;
    default:
      break;
    }
  }
}

static int
peek(re_compiler *c)
{
  if (c->p >= c->src_end) return -1;
  return (uint8_t)*c->p;
}

static int
next_char(re_compiler *c)
{
  if (c->p >= c->src_end) return -1;
  return (uint8_t)*c->p++;
}

static uint16_t
add_class(re_compiler *c)
{
  if (c->num_classes >= c->class_capa) {
    c->class_capa = c->class_capa ? c->class_capa * 2 : 8;
    c->classes = (re_charclass*)realloc(c->classes, sizeof(re_charclass) * c->class_capa);
  }
  uint16_t id = c->num_classes++;
  memset(&c->classes[id], 0, sizeof(re_charclass));
  return id;
}

static void
class_set_bit(re_charclass *cc, uint8_t ch)
{
  if (ch < 128) {
    cc->bitmap[ch >> 3] |= (1 << (ch & 7));
  }
}

/* Append a non-ASCII codepoint range [lo, hi]. Both bounds must be >= 128. */
static void
class_add_range(re_charclass *cc, uint32_t lo, uint32_t hi)
{
  /* Merge with the previous range when the new one is contiguous with or
     overlaps it. Codepoints are appended in scan order, so an ascending run
     (the common case, e.g. a long [...] enumeration) collapses to a single
     range instead of one entry per codepoint. */
  if (cc->num_ranges > 0) {
    uint32_t *last = &cc->ranges[2 * (cc->num_ranges - 1)];
    if (lo >= last[0] && lo <= last[1] + 1) {
      if (hi > last[1]) last[1] = hi;
      return;
    }
  }
  if (cc->num_ranges >= cc->range_capa) {
    /* range_capa/num_ranges are uint32_t: doubling from 32768 no longer
       wraps to 0 (which fed a size-0 realloc and a write through NULL). */
    uint32_t new_capa = cc->range_capa ? cc->range_capa * 2 : 4;
    cc->ranges = (uint32_t*)realloc(cc->ranges, sizeof(uint32_t) * 2 * new_capa);
    cc->range_capa = new_capa;
  }
  cc->ranges[2 * cc->num_ranges] = lo;
  cc->ranges[2 * cc->num_ranges + 1] = hi;
  cc->num_ranges++;
}

/* Add a single non-ASCII codepoint to the class. */
static void
class_add_codepoint(re_charclass *cc, uint32_t cp)
{
  class_add_range(cc, cp, cp);
}

static void
class_set_range(re_charclass *cc, uint8_t lo, uint8_t hi)
{
  for (int i = lo; i <= hi; i++) {
    class_set_bit(cc, (uint8_t)i);
  }
}

static void
class_add_shorthand(re_charclass *cc, int ch)
{
  switch (ch) {
  case 'd':
    class_set_range(cc, '0', '9');
    break;
  case 'D':
    class_set_range(cc, 0, '0'-1);
    class_set_range(cc, '9'+1, 127);
    cc->utf8_any = TRUE;
    break;
  case 'w':
    class_set_range(cc, 'a', 'z');
    class_set_range(cc, 'A', 'Z');
    class_set_range(cc, '0', '9');
    class_set_bit(cc, '_');
    break;
  case 'W':
    for (int i = 0; i < 128; i++) {
      if (!re_is_word_char(i)) class_set_bit(cc, (uint8_t)i);
    }
    cc->utf8_any = TRUE;
    break;
  case 's':
    class_set_bit(cc, ' ');
    class_set_bit(cc, '\t');
    class_set_bit(cc, '\n');
    class_set_bit(cc, '\r');
    class_set_bit(cc, '\f');
    class_set_bit(cc, '\v');
    break;
  case 'S':
    for (int i = 0; i < 128; i++) {
      if (i != ' ' && i != '\t' && i != '\n' && i != '\r' && i != '\f' && i != '\v')
        class_set_bit(cc, (uint8_t)i);
    }
    cc->utf8_any = TRUE;
    break;
  case 'h':
    /* hex digit: [0-9a-fA-F] */
    class_set_range(cc, '0', '9');
    class_set_range(cc, 'a', 'f');
    class_set_range(cc, 'A', 'F');
    break;
  case 'H':
    /* non-hex-digit: complement of [0-9a-fA-F]. Built as an explicit
       positive set so the top-level dispatcher can emit it as RE_CLASS
       and the `[...]` path can add it directly -- both contexts need the
       complement bits present (the uppercase->RE_NCLASS auto-route used
       by \D/\W/\S is deliberately bypassed for \H). */
    for (int i = 0; i < 128; i++) {
      mrb_bool is_hex = (i >= '0' && i <= '9') ||
                        (i >= 'a' && i <= 'f') ||
                        (i >= 'A' && i <= 'F');
      if (!is_hex) class_set_bit(cc, (uint8_t)i);
    }
    cc->utf8_any = TRUE;
    break;
  }
}

/* Add the ASCII range set for a POSIX bracket class `[:name:]`. Returns
   TRUE if `name` (length `len`) is a recognized class, FALSE otherwise so
   the caller can fall back to literal parsing. Semantics are the C/POSIX
   locale, which matches CRuby for ASCII input. Negation of the enclosing
   class is handled by the RE_NCLASS emit in compile_charclass, exactly as
   for the `\d`/`\w` shorthands, so these helpers only ever add the
   positive set. */
static mrb_bool
class_add_posix(re_charclass *cc, const char *name, size_t len)
{
#define POSIX_IS(s) (len == sizeof(s) - 1 && memcmp(name, s, len) == 0)
  if (POSIX_IS("alpha")) {
    class_set_range(cc, 'a', 'z');
    class_set_range(cc, 'A', 'Z');
  }
  else if (POSIX_IS("digit")) {
    class_set_range(cc, '0', '9');
  }
  else if (POSIX_IS("alnum")) {
    class_set_range(cc, 'a', 'z');
    class_set_range(cc, 'A', 'Z');
    class_set_range(cc, '0', '9');
  }
  else if (POSIX_IS("upper")) {
    class_set_range(cc, 'A', 'Z');
  }
  else if (POSIX_IS("lower")) {
    class_set_range(cc, 'a', 'z');
  }
  else if (POSIX_IS("space")) {
    /* [ \t\n\v\f\r] */
    class_set_range(cc, '\t', '\r');
    class_set_bit(cc, ' ');
  }
  else if (POSIX_IS("blank")) {
    class_set_bit(cc, ' ');
    class_set_bit(cc, '\t');
  }
  else if (POSIX_IS("xdigit")) {
    class_set_range(cc, '0', '9');
    class_set_range(cc, 'a', 'f');
    class_set_range(cc, 'A', 'F');
  }
  else if (POSIX_IS("word")) {
    class_set_range(cc, 'a', 'z');
    class_set_range(cc, 'A', 'Z');
    class_set_range(cc, '0', '9');
    class_set_bit(cc, '_');
  }
  else if (POSIX_IS("cntrl")) {
    class_set_range(cc, 0, 0x1f);
    class_set_bit(cc, 0x7f);
  }
  else if (POSIX_IS("print")) {
    /* printable, including space: 0x20-0x7e */
    class_set_range(cc, 0x20, 0x7e);
  }
  else if (POSIX_IS("graph")) {
    /* printable, excluding space: 0x21-0x7e */
    class_set_range(cc, 0x21, 0x7e);
  }
  else if (POSIX_IS("punct")) {
    /* printable non-alnum non-space ASCII */
    class_set_range(cc, '!', '/');
    class_set_range(cc, ':', '@');
    class_set_range(cc, '[', '`');
    class_set_range(cc, '{', '~');
  }
  else if (POSIX_IS("ascii")) {
    class_set_range(cc, 0, 0x7f);
  }
  else {
    return FALSE;
  }
  return TRUE;
#undef POSIX_IS
}

/* Negated POSIX class `[:^name:]`: add the ASCII complement of the named
   set plus utf8_any, mirroring how the `\D`/`\W`/`\S` shorthands build a
   positive complement set. ASCII input matches CRuby; non-ASCII follows
   the deferred-Unicode (B50) approximation, same as the shorthands.
   Returns FALSE for an unrecognized name so the caller can raise. */
static mrb_bool
class_add_posix_negated(re_charclass *cc, const char *name, size_t len)
{
  re_charclass tmp;
  memset(&tmp, 0, sizeof(tmp));
  if (!class_add_posix(&tmp, name, len)) return FALSE;
  for (int i = 0; i < 128; i++) {
    if (!(tmp.bitmap[i >> 3] & (1 << (i & 7)))) class_set_bit(cc, (uint8_t)i);
  }
  cc->utf8_any = TRUE;
  return TRUE;
}

static int
parse_escape(re_compiler *c)
{
  int ch = next_char(c);
  if (ch < 0) compile_error(c, "trailing backslash");
  switch (ch) {
  case 'n': return '\n';
  case 't': return '\t';
  case 'r': return '\r';
  case 'f': return '\f';
  case 'v': return '\v';
  case 'a': return '\a';
  case 'e': return 0x1b;
  /* `\b` is a word boundary outside a character class, but inside
     `[...]` it means backspace (U+0008). The outer compile loop
     consumes `\b` as RE_WBOUND before reaching parse_escape, so
     this arm only fires from read_class_atom -- i.e. always the
     character-class meaning. Issue #632. */
  case 'b': return 0x08;
  /* Octal escape `\NNN` (1-3 digits, value 0-255). The outer dispatcher
     consumes `\1`-`\9` as backref, so the only octal-leading digit
     that reaches here from the top level is `\0` -- but parse_escape
     also fires from read_class_atom inside `[...]`, where backref
     parsing does not apply, so the full 0-7 range needs handling. */
  case '0': case '1': case '2': case '3':
  case '4': case '5': case '6': case '7': {
    int val = ch - '0';
    int n = 1;
    while (n < 3) {
      int d = peek(c);
      if (d < '0' || d > '7') break;
      val = val * 8 + (d - '0');
      next_char(c);
      n++;
    }
    return val & 0xff;
  }
  /* Hex escape `\xHH` (1-2 hex digits, value 0-255). Spinel does not
     yet implement the `\x{HHHH}` form for codepoints above 0xff. */
  case 'x': {
    int val = 0;
    int n = 0;
    while (n < 2) {
      int d = peek(c);
      int v;
      if (d >= '0' && d <= '9') v = d - '0';
      else if (d >= 'a' && d <= 'f') v = d - 'a' + 10;
      else if (d >= 'A' && d <= 'F') v = d - 'A' + 10;
      else break;
      val = val * 16 + v;
      next_char(c);
      n++;
    }
    return val & 0xff;
  }
  default: return ch;  /* literal: \., \\, \/, \(, etc. */
  }
}

/* Read one character class atom: either an ASCII byte (0-127), a
   `\escape`, or a full multi-byte UTF-8 codepoint. Returns the
   codepoint and advances c->p. */
static uint32_t
read_class_atom(re_compiler *c)
{
  if (peek(c) == '\\') {
    next_char(c);
    return (uint32_t)parse_escape(c);
  }
  uint8_t b = (uint8_t)*c->p;
  if (b < 0xC0) {
    /* ASCII or stray continuation byte. */
    return (uint32_t)next_char(c);
  }
  /* Multi-byte UTF-8 leader: decode the full codepoint. The decoder is
     end-aware (truncated sequences consume a single byte), so no clamp. */
  int len = 0;
  uint32_t cp = re_utf8_decode(c->p, c->src_end, &len);
  c->p += len;
  return cp;
}

/* Parse [...] character class */
static void
compile_charclass(re_compiler *c)
{
  uint16_t id = add_class(c);
  re_charclass *cc = &c->classes[id];
  mrb_bool negated = FALSE;

  if (peek(c) == '^') {
    next_char(c);
    negated = TRUE;
  }

  mrb_bool first = TRUE;
  while (peek(c) != ']' || first) {
    if (peek(c) < 0) compile_error(c, "unterminated character class");
    first = FALSE;

    /* Shorthand classes (\d, \D, \w, \W, \s, \S) are handled before
       the codepoint-aware path so the single-byte semantics stay
       intact. */
    if (peek(c) == '\\') {
      int esc = (c->p + 1 < c->src_end) ? (uint8_t)c->p[1] : -1;
      if (esc == 'd' || esc == 'D' || esc == 'w' || esc == 'W' ||
          esc == 's' || esc == 'S' || esc == 'h' || esc == 'H') {
        next_char(c);  /* '\\' */
        next_char(c);  /* spec  */
        class_add_shorthand(cc, esc);
        continue;
      }
    }

    /* POSIX bracket class `[:name:]` inside the enclosing `[...]`. Adds
       the corresponding ASCII ranges via the same helpers used for `\d`
       etc.; enclosing negation is applied by the RE_NCLASS emit below. A
       leading `^` (`[:^name:]`) negates the class in place. A complete
       `[:...:]` with an unrecognized name is a hard error in CRuby
       ("invalid POSIX bracket type"); raise rather than match nothing. */
    if (peek(c) == '[' && c->p + 1 < c->src_end && c->p[1] == ':') {
      const char *name = c->p + 2;
      mrb_bool posix_neg = FALSE;
      if (name < c->src_end && *name == '^') { posix_neg = TRUE; name++; }
      const char *q = name;
      while (q < c->src_end && *q != ':' && *q != ']') q++;
      if (q + 1 < c->src_end && q[0] == ':' && q[1] == ']') {
        size_t nlen = (size_t)(q - name);
        mrb_bool ok = posix_neg ? class_add_posix_negated(cc, name, nlen)
                                : class_add_posix(cc, name, nlen);
        if (!ok) compile_error(c, "invalid POSIX bracket type");
        c->p = q + 2;  /* consume past ":]" */
        continue;
      }
      /* No `:]` terminator: not a POSIX class. Fall through and treat
         '[' literally (e.g. `[[:]` is the literal set {'[', ':'}). */
    }

    uint32_t cp = read_class_atom(c);

    /* check for range a-z (or U+xxxx-U+yyyy) */
    if (peek(c) == '-' && c->p + 1 < c->src_end && c->p[1] != ']') {
      next_char(c);  /* skip '-' */
      uint32_t hi = read_class_atom(c);
      /* Issue #778: reversed range like [z-a] is a hard error in
         CRuby (RegexpError "empty range in char class"). Spinel
         used to silently accept it and emit a class that matched
         nothing. Raise instead. */
      if (cp > hi) {
        compile_error(c, "empty range in char class");
      }
      if (cp < 128 && hi < 128) {
        class_set_range(cc, (uint8_t)cp, (uint8_t)hi);
      }
      else {
        /* Range that touches non-ASCII: store as codepoint range.
           Mixed ASCII/non-ASCII ranges are rare; stash the whole
           span in the codepoint list (the bitmap covers ASCII only,
           so a non-ASCII upper bound forces the codepoint path). */
        class_add_range(cc, cp, hi);
      }
    }
    else {
      if (cp < 128) class_set_bit(cc, (uint8_t)cp);
      else class_add_codepoint(cc, cp);
    }
  }
  next_char(c);  /* skip ']' */

  /* /i: case-fold the ASCII letter bits so [a-z] under IGNORECASE matches
     both cases (the fold runs on the positive set; RE_NCLASS negation at
     exec time then excludes both cases, matching CRuby). */
  if (c->flags & RE_FLAG_IGNORECASE) {
    for (int lc = 'a'; lc <= 'z'; lc++) {
      int uc = lc - 32;
      if (cc->bitmap[lc >> 3] & (1 << (lc & 7))) class_set_bit(cc, (uint8_t)uc);
      if (cc->bitmap[uc >> 3] & (1 << (uc & 7))) class_set_bit(cc, (uint8_t)lc);
    }
  }

  cc->negated = negated;
  emit(c, negated ? RE_NCLASS : RE_CLASS, (uint8_t)id, 0);
}

/* Parse {n}, {n,}, {n,m} quantifier. Returns min,max via pointers. */
static mrb_bool
parse_quantifier(re_compiler *c, int *min_out, int *max_out)
{
  const char *save = c->p;
  /* Issue #819: integer overflow in `min/max = ... * 10 + digit`
     used to wrap to negative. CRuby caps quantifiers at a sane
     internal limit. We pick 100000 -- larger than any reasonable
     pattern but small enough that the subsequent emit loop can't
     run forever. */
  enum { RE_QUANT_MAX = 100000 };
  int min = 0, max = -1;

  while (peek(c) >= '0' && peek(c) <= '9') {
    int d = next_char(c) - '0';
    if (min > RE_QUANT_MAX || min * 10 + d > RE_QUANT_MAX) {
      compile_error(c, "quantifier too big");
    }
    min = min * 10 + d;
  }
  if (peek(c) == ',') {
    next_char(c);
    if (peek(c) >= '0' && peek(c) <= '9') {
      max = 0;
      while (peek(c) >= '0' && peek(c) <= '9') {
        int d = next_char(c) - '0';
        if (max > RE_QUANT_MAX || max * 10 + d > RE_QUANT_MAX) {
          compile_error(c, "quantifier too big");
        }
        max = max * 10 + d;
      }
    }
    /* else max = -1 (unlimited) */
  }
  else {
    max = min;  /* {n} means exactly n */
  }
  if (peek(c) != '}') {
    c->p = save;  /* not a quantifier, treat { as literal */
    return FALSE;
  }
  next_char(c);  /* skip '}' */
  /* Issue #822: min > max is invalid. CRuby raises RegexpError. */
  if (max >= 0 && min > max) {
    compile_error(c, "invalid repeat count");
  }
  *min_out = min;
  *max_out = max;
  return TRUE;
}

/*
 * Compute the fixed byte length consumed by bytecode in range [start, end).
 * Returns -1 if the pattern has variable length (quantifiers, alternation
 * with different-length branches, etc.).
 * Used for lookbehind: we need to know exactly how far back to look.
 */
static int
compute_fixed_len(re_compiler *c, uint32_t start, uint32_t end)
{
  int len = 0;
  uint32_t pc = start;

  while (pc < end) {
    re_inst inst = c->code[pc];
    switch (inst.op) {
    case RE_CHAR:
    case RE_CLASS:
    case RE_NCLASS:
      len += 1;
      pc++;
      break;
    case RE_ANY:
    case RE_ANY_NL:
      /* . matches one character which can be 1-4 bytes in UTF-8.
         For ASCII-only mode this is 1 byte; for safety, only allow
         if we can determine it's ASCII context. Return -1 for now. */
      return -1;
    case RE_SAVE:
      pc++;
      break;  /* zero-width */
    case RE_BOL: case RE_EOL: case RE_BOT: case RE_EOT: case RE_EOTNL:
    case RE_WBOUND: case RE_NWBOUND:
      pc++;
      break;  /* zero-width assertions */
    case RE_JMP:
      pc = inst.offset;
      break;
    case RE_SPLIT: {
      /* alternation: both branches must have the same fixed length */
      /* branch 1: pc+1 to next JMP before branch 2 */
      /* branch 2: inst.offset to ... */
      /* For simplicity, reject alternation in lookbehind */
      return -1;
    }
    case RE_MATCH:
      return len;
    default:
      return -1;  /* unknown/variable-length instruction */
    }
  }
  return len;
}

/* Parse the option letters of an inline (?...) group. The parser is
   positioned just past the '?'; it reads a run of i/m/x, an optional '-',
   and a further run of i/m/x to switch off, then stops at the terminator
   (':' or ')'). `base` is the option set in effect on entry; the resulting
   set is returned. Ruby's inline letters are i (IGNORECASE), m (DOTALL),
   x (EXTENDED). Extended mode is applied by a whole-pattern preprocessing
   pass and cannot be scoped inline; `*want_x` reports that an x (not
   switched back off) was requested, and the caller accepts it only when
   it is provably a no-op (no significant whitespace in its scope). */
static uint32_t
parse_inline_flags(re_compiler *c, uint32_t base, mrb_bool *want_x)
{
  uint32_t on = 0, off = 0;
  mrb_bool negate = FALSE, seen = FALSE, x_on = FALSE, x_off = FALSE;
  *want_x = FALSE;
  for (;;) {
    int oc = peek(c);
    uint32_t bit;
    if (oc == 'i') bit = RE_FLAG_IGNORECASE;
    else if (oc == 'm') bit = RE_FLAG_DOTALL;
    else if (oc == 'x') {
      if (negate) x_off = TRUE;
      else x_on = TRUE;
      seen = TRUE;
      next_char(c);
      continue;
    }
    else if (oc == '-' && !negate) { negate = TRUE; next_char(c); continue; }
    else break;
    if (negate) off |= bit;
    else on |= bit;
    seen = TRUE;
    next_char(c);
  }
  if (!seen) compile_error(c, "undefined (?...) sequence");
  *want_x = (x_on && !x_off);
  return (base | on) & ~off;
}

/* Whether the range [p, group end) contains whitespace or `#` that inline
   extended mode would treat specially: whitespace/comments OUTSIDE character
   classes (inside [...] they are literal even under /x, and an escaped
   space means a literal space with or without /x). The scope ends at the
   enclosing group's ')' (depth-aware) or the pattern end. Used to accept a
   decorative (?x...) -- one whose x changes nothing -- while raising on one
   whose stripping semantics spinel does not implement. */
static mrb_bool
inline_x_scope_has_significant_ws(const char *p, const char *end)
{
  int depth = 0;
  mrb_bool in_class = FALSE;
  for (; p < end; p++) {
    char ch = *p;
    if (ch == '\\') { p++; continue; }   /* escaped char: literal either way */
    if (in_class) {
      if (ch == ']') in_class = FALSE;
      continue;
    }
    if (ch == '[') { in_class = TRUE; continue; }
    if (ch == '(') { depth++; continue; }
    if (ch == ')') { if (depth == 0) return FALSE; depth--; continue; }
    if (ch == '#' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
        ch == '\f' || ch == '\v')
      return TRUE;
  }
  return FALSE;
}

/* Compile a single atom (character, class, group, etc.) */
static void
compile_atom(re_compiler *c)
{
  int ch = peek(c);

  switch (ch) {
  case '(':
    {
      next_char(c);
      mrb_bool capturing = TRUE;

      /* Options in effect on entry. A group restores them on exit so an
         inline toggle like (?i) inside it (which sets c->flags for the rest
         of the group) does not leak past the closing ')'. */
      uint32_t saved_flags = c->flags;

      const char *cap_name = NULL;
      uint16_t cap_name_len = 0;

      if (peek(c) == '?' && c->p + 1 < c->src_end) {
        if (c->p[1] == '#') {
          /* (?#comment): skip to the group-closing ')' -- comments cannot
             contain one (Ruby allows no escaping inside (?#...)) -- and
             continue with the NEXT atom; this group emits nothing. */
          next_char(c); next_char(c);  /* skip ?# */
          while (c->p < c->src_end && peek(c) != ')') next_char(c);
          if (peek(c) != ')') compile_error(c, "unterminated (?#comment");
          next_char(c);
          return;  /* an empty atom: emits nothing */
        }
        if (c->p[1] == ':') {
          next_char(c); next_char(c);  /* skip ?: */
          capturing = FALSE;
        }
        else if (c->p[1] == '=' || c->p[1] == '!') {
          /* lookahead (?=...) or (?!...) */
          mrb_bool negative = (c->p[1] == '!');
          next_char(c); next_char(c);  /* skip ?= or ?! */
          uint32_t la_pos = emit(c, negative ? RE_NEG_LOOKAHEAD : RE_LOOKAHEAD, 0, 0);
          compile_alt(c);
          emit(c, RE_MATCH, 0, 0);  /* end of lookahead sub-pattern */
          c->code[la_pos].offset = (uint16_t)c->code_len;  /* patch: skip past sub-pattern */
          if (peek(c) != ')') compile_error(c, "unmatched '('");
          next_char(c);
          c->needs_backtrack = TRUE;  /* needs backtracking engine */
          c->flags = saved_flags;
          break;  /* done with this atom */
        }
        else if (c->p[1] == '<' && c->p + 2 < c->src_end && (c->p[2] == '=' || c->p[2] == '!')) {
          /* lookbehind (?<=...) or (?<!...) */
          mrb_bool negative = (c->p[2] == '!');
          next_char(c); next_char(c); next_char(c);  /* skip ?<= or ?<! */
          uint32_t lb_pos = emit(c, negative ? RE_NEG_LOOKBEHIND : RE_LOOKBEHIND, 0, 0);
          uint32_t sub_start = c->code_len;
          compile_alt(c);
          emit(c, RE_MATCH, 0, 0);
          c->code[lb_pos].offset = (uint16_t)c->code_len;

          /* compute fixed byte length of lookbehind sub-pattern */
          int fixed_len = compute_fixed_len(c, sub_start, c->code_len);
          if (fixed_len < 0) {
            compile_error(c, "lookbehind must be fixed length");
          }
          if (fixed_len > 255) {
            compile_error(c, "lookbehind too long (max 255 bytes)");
          }
          c->code[lb_pos].a = (uint8_t)fixed_len;

          if (peek(c) != ')') compile_error(c, "unmatched '('");
          next_char(c);
          c->needs_backtrack = TRUE;  /* needs backtracking engine */
          c->flags = saved_flags;
          break;
        }
        else if (c->p[1] == '<' && c->p + 2 < c->src_end && c->p[2] != '=' && c->p[2] != '!') {
          next_char(c); next_char(c);  /* skip ?< */
          cap_name = c->p;
          while (peek(c) != '>' && peek(c) >= 0) next_char(c);
          if (peek(c) != '>') compile_error(c, "unterminated named capture");
          cap_name_len = (uint16_t)(c->p - cap_name);
          next_char(c);  /* skip > */
        }
        else if (c->p[1] == 'i' || c->p[1] == 'm' || c->p[1] == 'x' || c->p[1] == '-') {
          /* Inline options (imported from mruby): the toggle form
             (?imx) / (?-imx) changes the options for the rest of the
             enclosing group, and the scoped form (?imx:...) is a
             non-capturing group whose options apply only to its body.
             The emit path already consults c->flags for IGNORECASE and
             DOTALL, so tracking the flags is all i/m need. Inline x is
             accepted only when it is a no-op (its scope has no
             significant whitespace or `#`); a scope the strip would
             actually change raises rather than silently diverging. */
          next_char(c);  /* skip '?' */
          mrb_bool want_x = FALSE;
          uint32_t new_flags = parse_inline_flags(c, c->flags, &want_x);
          if (peek(c) == ')') {
            next_char(c);
            if (want_x && inline_x_scope_has_significant_ws(c->p, c->src_end))
              compile_error(c, "inline extended mode (?x) with significant whitespace is not supported");
            c->flags = new_flags;  /* rest of the group; restored at its ')' */
            return;                /* consumed the token; no atom emitted */
          }
          else if (peek(c) == ':') {
            next_char(c);
            if (want_x && inline_x_scope_has_significant_ws(c->p, c->src_end))
              compile_error(c, "inline extended mode (?x) with significant whitespace is not supported");
            c->flags = new_flags;
            compile_alt(c);
            c->flags = saved_flags;
            if (peek(c) != ')') compile_error(c, "unmatched '('");
            next_char(c);
            return;
          }
          else {
            compile_error(c, "undefined (?...) sequence");
          }
        }
        else {
          /* (?X) with an unsupported X: not one of the recognized (?: (?= (?!
             (?<= (?<! (?<name> (?imx forms. The absent operator (?~...) and
             conditionals (?(...)) are not implemented. Raise here rather than
             falling through to the capturing-group path, which would leave
             the stray `?` for compile_seq to spin on forever. */
          compile_error(c, "undefined (?...) sequence");
        }
      }

      uint16_t group = 0;
      if (capturing) {
        if (c->num_captures >= RE_MAX_CAPTURES) {
          compile_error(c, "too many capture groups");
        }
        group = c->num_captures++;
        emit(c, RE_SAVE, 0, group * 2);
        if (cap_name) {
          /* register named capture */
          /* Issue #823: cap_name points into the pattern source --
             but when the pattern came from strip_extended, c->stripped
             is freed at re_compile exit, leaving the name dangling.
             Allocate a fresh copy so the named-capture table owns
             its strings. */
          char *name_copy = (char*)malloc(cap_name_len + 1);
          if (!name_copy) compile_error(c, "out of memory");
          memcpy(name_copy, cap_name, cap_name_len);
          name_copy[cap_name_len] = '\0';
          c->named_captures = (re_named_capture*)realloc(c->named_captures,
            sizeof(re_named_capture) * (c->num_named + 1));
          c->named_captures[c->num_named].name = name_copy;
          c->named_captures[c->num_named].name_len = cap_name_len;
          c->named_captures[c->num_named].group = group;
          c->num_named++;
        }
      }

      compile_alt(c);

      if (peek(c) != ')') compile_error(c, "unmatched '('");
      next_char(c);

      if (capturing) {
        emit(c, RE_SAVE, 0, group * 2 + 1);
      }
      c->flags = saved_flags;  /* inline toggles inside the group end here */
    }
    break;

  case '[':
    next_char(c);
    compile_charclass(c);
    break;

  case '.':
    next_char(c);
    emit(c, (c->flags & RE_FLAG_DOTALL) ? RE_ANY_NL : RE_ANY, 0, 0);
    break;

  case '^':
    next_char(c);
    emit(c, RE_BOL, 0, 0);
    break;

  case '$':
    next_char(c);
    emit(c, RE_EOL, 0, 0);
    break;

  case '\\':
    next_char(c);
    ch = peek(c);
    if (ch >= '1' && ch <= '9') {
      next_char(c);
      emit(c, RE_BACKREF, (uint8_t)(ch - '0'), 0);
      c->has_backref = TRUE;
    }
    else if (ch == 'd' || ch == 'D' || ch == 'w' || ch == 'W' || ch == 's' || ch == 'S') {
      next_char(c);
      uint16_t id = add_class(c);
      class_add_shorthand(&c->classes[id], ch);
      /* class_add_shorthand already builds the directly-matching set for
         every shorthand -- positive for d/w/s, the explicit complement
         (plus utf8_any) for D/W/S -- so emit RE_CLASS for all of them.
         The old `uppercase -> RE_NCLASS` route negated the complement a
         second time, so top-level \D/\W/\S matched exactly the set they
         should reject. The `[...]` path was unaffected (no NCLASS wrapper)
         and stays correct. Mirrors the \h/\H arm below. */
      emit(c, RE_CLASS, (uint8_t)id, 0);
    }
    else if (ch == 'h' || ch == 'H') {
      /* \h / \H both carry their full positive set (hex digits /
         non-hex-digits), so emit RE_CLASS for both rather than routing
         \H through the uppercase RE_NCLASS path. */
      next_char(c);
      uint16_t id = add_class(c);
      class_add_shorthand(&c->classes[id], ch);
      emit(c, RE_CLASS, (uint8_t)id, 0);
    }
    else if (ch == 'A') {
      next_char(c);
      emit(c, RE_BOT, 0, 0);
    }
    else if (ch == 'z') {
      next_char(c);
      emit(c, RE_EOT, 0, 0);
    }
    else if (ch == 'Z') {
      next_char(c);
      emit(c, RE_EOTNL, 0, 0);
    }
    else if (ch == 'b') {
      next_char(c);
      emit(c, RE_WBOUND, 0, 0);
    }
    else if (ch == 'B') {
      next_char(c);
      emit(c, RE_NWBOUND, 0, 0);
    }
    else if (ch == 'k' && c->p + 1 < c->src_end &&
             (c->p[1] == '<' || c->p[1] == '\'')) {
      /* \k<name> / \k'name': backreference to a named group. Numeric forms
         \k<2> (absolute) and \k<-1> (relative to the groups seen so far) are
         also accepted, like the \g/\k family in Onigmo. */
      next_char(c);  /* skip k */
      int close = (peek(c) == '<') ? '>' : '\'';
      next_char(c);  /* skip < or ' */
      const char *name = c->p;
      while (peek(c) != close && peek(c) >= 0) next_char(c);
      if (peek(c) != close) compile_error(c, "unterminated backreference name");
      uint16_t name_len = (uint16_t)(c->p - name);
      next_char(c);  /* skip the closing > or ' */

      int group = -1;
      if (name_len > 0 && (name[0] == '-' || (name[0] >= '0' && name[0] <= '9'))) {
        mrb_bool relative = (name[0] == '-');
        int n = 0;
        for (uint16_t i = (relative ? 1 : 0); i < name_len; i++) {
          if (name[i] < '0' || name[i] > '9') compile_error(c, "invalid backreference");
          n = n * 10 + (name[i] - '0');
        }
        group = relative ? (int)c->num_captures - n : n;
      }
      else {
        for (uint16_t i = 0; i < c->num_named; i++) {
          if (c->named_captures[i].name_len == name_len &&
              memcmp(c->named_captures[i].name, name, name_len) == 0) {
            group = c->named_captures[i].group;
            break;
          }
        }
      }
      if (group < 1 || group >= (int)c->num_captures) {
        compile_error(c, "undefined group name reference");
      }
      emit(c, RE_BACKREF, (uint8_t)group, 0);
      c->has_backref = TRUE;
    }
    else {
      ch = parse_escape(c);
      if (c->flags & RE_FLAG_IGNORECASE) {
        if (ch >= 'A' && ch <= 'Z') {
          uint16_t id = add_class(c);
          class_set_bit(&c->classes[id], (uint8_t)ch);
          class_set_bit(&c->classes[id], (uint8_t)(ch + 32));
          emit(c, RE_CLASS, (uint8_t)id, 0);
          break;
        }
        else if (ch >= 'a' && ch <= 'z') {
          uint16_t id = add_class(c);
          class_set_bit(&c->classes[id], (uint8_t)ch);
          class_set_bit(&c->classes[id], (uint8_t)(ch - 32));
          emit(c, RE_CLASS, (uint8_t)id, 0);
          break;
        }
      }
      emit(c, RE_CHAR, (uint8_t)ch, 0);
    }
    break;

  default:
    if (ch < 0 || ch == ')' || ch == '|' || ch == '*' || ch == '+' || ch == '?') {
      return;  /* not an atom */
    }
    /* `{` with no preceding atom (or after one whose quantifier
       parse failed) is a literal `{`. Without this, compile_seq's
       outer loop spins -- compile_quantified returns no-atom and
       the loop never advances. CRuby treats `/{re}/` as matching
       the literal text `{re}`; we mirror that here. Issue #548. */
    next_char(c);
    if ((c->flags & RE_FLAG_IGNORECASE) && ch < 128) {
      if (ch >= 'A' && ch <= 'Z') {
        uint16_t id = add_class(c);
        class_set_bit(&c->classes[id], (uint8_t)ch);
        class_set_bit(&c->classes[id], (uint8_t)(ch + 32));
        emit(c, RE_CLASS, (uint8_t)id, 0);
        break;
      }
      else if (ch >= 'a' && ch <= 'z') {
        uint16_t id = add_class(c);
        class_set_bit(&c->classes[id], (uint8_t)ch);
        class_set_bit(&c->classes[id], (uint8_t)(ch - 32));
        emit(c, RE_CLASS, (uint8_t)id, 0);
        break;
      }
    }
    emit(c, RE_CHAR, (uint8_t)ch, 0);
    break;
  }
}

/* Append a copy of the atom bytecode in [start, start+size) at the current
   position. Internal jump/split targets are relocated to the copy, so a
   repeated group like (a{2,3}){2} keeps each iteration self-contained instead
   of jumping back into the first copy (which corrupted its captures). Capture
   slots (RE_SAVE) are shared across copies on purpose: a repeated group keeps
   only its last iteration, like CRuby. */
static void
emit_atom_copy(re_compiler *c, uint32_t start, uint32_t size)
{
  int32_t delta = (int32_t)c->code_len - (int32_t)start;
  uint32_t atom_end = start + size;
  for (uint32_t j = 0; j < size; j++) {
    re_inst in = c->code[start + j];
    switch (in.op) {
    case RE_JMP: case RE_SPLIT: case RE_SPLITNG:
      if (in.offset >= start && in.offset <= atom_end) {
        in.offset = (uint16_t)((int32_t)in.offset + delta);
      }
      break;
    default:
      break;
    }
    emit(c, in.op, in.a, in.offset);
  }
}

/* Compile atom with quantifiers (*, +, ?, {n,m}) */
static void
compile_quantified(re_compiler *c)
{
  uint32_t start = c->code_len;
  compile_atom(c);
  if (c->code_len == start) {
    /* Issue #825: when compile_atom emitted nothing AND the next
       char is a bare quantifier (star, plus, question), the
       surrounding seq loop has nothing to advance with and spins
       forever. Raise instead. CRuby: RegexpError "target of
       repeat operator is not specified". */
    int qch = peek(c);
    if (qch == '*' || qch == '+' || qch == '?') {
      compile_error(c, "target of repeat operator is not specified");
    }
    return;  /* no atom emitted, no quantifier -- caller handles */
  }

  int ch = peek(c);
  if (ch == '*' || ch == '+' || ch == '?') {
    next_char(c);
    mrb_bool nongreedy = (peek(c) == '?');
    if (nongreedy) {
      next_char(c);
      c->needs_backtrack = TRUE;
    }


    if (ch == '*') {
      /* e* → L: SPLIT(body, end); body; JMP L; end:
         SPLIT offset = end (after JMP), patched after JMP is emitted */
      insert_inst(c, start, nongreedy ? RE_SPLITNG : RE_SPLIT, 0, 0);
      emit(c, RE_JMP, 0, start);
      c->code[start].offset = (uint16_t)c->code_len;  /* patch: skip to end */
    }
    else if (ch == '+') {
      /* e+ → body; SPLIT/SPLITNG(start)
         SPLIT: first=pc+1(end), second=offset(start) → non-greedy
         SPLITNG: first=offset(start), second=pc+1(end) → greedy */
      emit(c, nongreedy ? RE_SPLIT : RE_SPLITNG, 0, start);
    }
    else { /* ? */
      /* e? → SPLIT(body, end); body; end: */
      insert_inst(c, start, nongreedy ? RE_SPLITNG : RE_SPLIT, 0, 0);
      c->code[start].offset = (uint16_t)c->code_len;  /* patch: skip to end */
    }
  }
  else if (ch == '{') {
    const char *save = c->p;
    next_char(c);
    int min, max;
    if (!parse_quantifier(c, &min, &max)) {
      c->p = save;
      return;  /* not a quantifier */
    }
    mrb_bool nongreedy = (peek(c) == '?');
    if (nongreedy) {
      next_char(c);
      c->needs_backtrack = TRUE;
    }

    /* For {n,m}: repeat atom min times, then optional (max-min) times */
    uint32_t atom_end = c->code_len;
    uint32_t atom_size = atom_end - start;

    /* The atom was emitted once up front and counted as the first mandatory
       copy. That copy is wrong when the lower bound is zero (mruby e246b2c05):
       a{0,3} matched up to four, a{0} matched one, a{0,} behaved like a+. */
    if (min == 0 && max == 0) {
      /* {0}: the atom matches zero times, so drop the copy we emitted. */
      c->code_len = start;
    }
    else {
      /* {0,m} and {0,} compile as {1,m}/{1,} wrapped in an optional, so the
         single already-emitted copy is not forced to match. lo is the lower
         bound used while laying out copies (1 in the wrapped case). */
      mrb_bool wrap_optional = (min == 0);
      int lo = wrap_optional ? 1 : min;

      /* We have one copy already; emit lo-1 more mandatory copies. */
      for (int i = 1; i < lo; i++) {
        emit_atom_copy(c, start, atom_size);
      }
      /* Then optional copies */
      if (max < 0) {
        /* {n,} = lo copies + * */
        uint32_t loop_start = c->code_len;
        uint32_t split_pos = emit(c, nongreedy ? RE_SPLITNG : RE_SPLIT, 0, 0);
        emit_atom_copy(c, start, atom_size);
        emit(c, RE_JMP, 0, loop_start);
        patch(c, split_pos, c->code_len);
      }
      else {
        for (int i = lo; i < max; i++) {
          uint32_t split_pos = emit(c, nongreedy ? RE_SPLITNG : RE_SPLIT, 0, 0);
          emit_atom_copy(c, start, atom_size);
          patch(c, split_pos, c->code_len);
        }
      }
      if (wrap_optional) {
        /* Make the whole {1,m}/{1,} body skippable so it matches zero times. */
        insert_inst(c, start, nongreedy ? RE_SPLITNG : RE_SPLIT, 0, 0);
        c->code[start].offset = (uint16_t)c->code_len;
      }
    }
  }
}

/* Compile a sequence of quantified atoms */
static void
compile_seq(re_compiler *c)
{
  while (peek(c) >= 0 && peek(c) != ')' && peek(c) != '|') {
    uint32_t code_before = c->code_len;
    const char *p_before = c->p;
    compile_quantified(c);
    if (c->code_len == code_before && c->p == p_before) {
      /* compile_quantified neither consumed input nor emitted code: the
         current character is a quantifier metacharacter with no atom to
         repeat (a leading `*`, `+`, `?`, or the trailing `*`s in `a***`).
         CRuby raises RegexpError here; without this guard peek() never
         advances and the loop spins forever. */
      compile_error(c, "target of repeat operator is not specified");
    }
  }
}

/* Compile alternation: seq | seq | ... */
static void
compile_alt(re_compiler *c)
{
  uint32_t alt_start = c->code_len;
  compile_seq(c);

  if (peek(c) != '|') return;

  /* a|b → SPLIT L1 L2; L1: a; JMP END; L2: b; END:
     We need to insert SPLIT before already-emitted code for first alt.
     Strategy: emit JMP after first alt, then for each subsequent alt,
     insert a SPLIT before it by shifting code. */

  /* Collect all alternatives, then emit SPLIT chain at the end.
     This avoids insert_inst offset corruption for multi-way alternation.
     alt_starts grows dynamically; the only ceiling is the offset field's
     uint16_t width (~65535), enforced later when the SPLIT chain is
     wired up. Issue #777. */
  uint32_t alt_cap = 64;
  uint32_t *alt_starts = (uint32_t *)malloc(sizeof(uint32_t) * alt_cap);
  if (!alt_starts) compile_error(c, "out of memory");
  uint32_t num_alts = 0;
  alt_starts[num_alts++] = alt_start;

  while (peek(c) == '|') {
    next_char(c);
    emit(c, RE_JMP, 0, 0);  /* placeholder: jump to end */
    if (num_alts >= alt_cap) {
      alt_cap *= 2;
      uint32_t *grown = (uint32_t *)realloc(alt_starts, sizeof(uint32_t) * alt_cap);
      if (!grown) { free(alt_starts); compile_error(c, "out of memory"); }
      alt_starts = grown;
    }
    alt_starts[num_alts++] = c->code_len;
    compile_seq(c);
  }

  if (num_alts <= 1) { free(alt_starts); return; }

  /* Now insert SPLIT chain before the alternatives.
     For n alternatives: n-1 SPLIT instructions, each pointing to
     their respective alternative. */
  uint32_t split_count = num_alts - 1;
  /* Insert split_count instructions at alt_starts[0] */
  for (uint32_t i = 0; i < split_count; i++) {
    insert_inst(c, alt_starts[0], RE_JMP, 0, 0);  /* placeholder */
    /* adjust all alt_starts by +1 due to insertion */
    for (uint32_t j = 0; j < num_alts; j++) {
      alt_starts[j]++;
    }
  }

  /* Now set up the SPLIT chain. Each SPLIT falls through to the next, and the
     chain's final fall-through reaches the first alternative, so the engines
     (which rank a SPLIT's fall-through above its jump) explore alternative 0
     first. The jump targets are then unwound in reverse, so SPLIT i must jump
     to alternative (split_count - i) to keep the remaining alternatives in
     source order -- i.e. leftmost-first across three or more branches. */
  for (uint32_t i = 0; i < split_count; i++) {
    uint32_t pos = alt_starts[0] - split_count + i;
    uint32_t target = alt_starts[split_count - i];
    if (target > 0xFFFF) {
      free(alt_starts);
      compile_error(c, "regex too large (alternation offset overflow)");
    }
    c->code[pos].op = RE_SPLIT;
    c->code[pos].a = 0;
    c->code[pos].offset = (uint16_t)target;
  }

  /* Patch JMPs (they are right before each alt_starts[1..n-1]) to point to end */
  uint32_t end = c->code_len;
  if (end > 0xFFFF) {
    free(alt_starts);
    compile_error(c, "regex too large (end offset overflow)");
  }
  for (uint32_t i = 1; i < num_alts; i++) {
    uint32_t jmp_pos = alt_starts[i] - 1;
    c->code[jmp_pos].op = RE_JMP;
    c->code[jmp_pos].offset = (uint16_t)end;
  }
  free(alt_starts);
}

/*
 * Strip whitespace and #comments for extended mode (/x flag).
 * Whitespace inside [...] character classes is preserved.
 * Escaped characters (\ followed by anything) are preserved.
 */
static char*
strip_extended(const char *src, mrb_int len, mrb_int *out_len)
{
  char *buf = (char*)malloc(len);
  mrb_int o = 0;
  mrb_bool in_class = FALSE;
  const char *end = src + len;

  while (src < end) {
    char ch = *src;
    if (ch == '\\' && src + 1 < end) {
      buf[o++] = *src++;
      buf[o++] = *src++;
      continue;
    }
    if (in_class) {
      if (ch == ']') in_class = FALSE;
      buf[o++] = *src++;
      continue;
    }
    if (ch == '[') {
      in_class = TRUE;
      buf[o++] = *src++;
      continue;
    }
    if (ch == '#') {
      /* skip to end of line */
      while (src < end && *src != '\n') src++;
      continue;
    }
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v') {
      src++;
      continue;
    }
    buf[o++] = *src++;
  }
  *out_len = o;
  return buf;
}

/*
 * Compute the set of bytes that could be the first consumed byte of a match.
 * Walks bytecode from pc=0, following epsilon transitions (SAVE, JMP, SPLIT).
 * Returns TRUE if the set is narrower than "any byte" (i.e., useful for skip).
 */
static mrb_bool
first_set_walk(const re_inst *code, uint32_t code_len,
               const re_charclass *classes, uint32_t pc,
               uint8_t *bm, uint8_t *seen)
{
  while (pc < code_len) {
    if (seen[pc]) return TRUE;  /* already visited */
    seen[pc] = 1;
    switch (code[pc].op) {
    case RE_SAVE:
    case RE_BOL: case RE_EOL: case RE_BOT: case RE_EOT: case RE_EOTNL:
    case RE_WBOUND: case RE_NWBOUND:
      pc++;
      continue;  /* zero-width, keep walking */
    case RE_JMP:
      pc = code[pc].offset;
      continue;
    case RE_SPLIT:
      /* both branches: pc+1 and offset */
      if (!first_set_walk(code, code_len, classes, code[pc].offset, bm, seen))
        return FALSE;
      pc++;
      continue;
    case RE_SPLITNG:
      if (!first_set_walk(code, code_len, classes, pc + 1, bm, seen))
        return FALSE;
      pc = code[pc].offset;
      continue;
    case RE_CHAR:
      bm[code[pc].a >> 3] |= (1 << (code[pc].a & 7));
      return TRUE;
    case RE_CLASS: {
      const re_charclass *cc = &classes[code[pc].a];
      for (int i = 0; i < 16; i++) bm[i] |= cc->bitmap[i];
      if (cc->utf8_any) return FALSE;  /* non-ASCII possible */
      if (cc->num_ranges > 0) return FALSE;  /* non-ASCII codepoints possible */
      return TRUE;
    }
    case RE_NCLASS: {
      /* negated class: complement of bitmap. Too many bits; not useful. */
      return FALSE;
    }
    case RE_ANY: case RE_ANY_NL:
      return FALSE;  /* any byte possible */
    case RE_MATCH:
      /* Reaching MATCH via epsilon transitions means the regex can match
         zero characters at any position. Skipping bytes that aren't in the
         first-byte set would skip past valid empty-match positions, so the
         optimization isn't safe -- bail out and accept any starting byte.
         Imported from mruby d21eceb286. */
      return FALSE;
    default:
      return FALSE;
    }
  }
  /* Walked off the end without hitting MATCH or a consuming op. Treat as
     empty-matchable, same as RE_MATCH. */
  return FALSE;
}

static mrb_bool
compute_first_set(const re_inst *code, uint32_t code_len,
                  const re_charclass *classes, uint8_t *bm)
{
  uint8_t seen[4096];
  if (code_len >= sizeof(seen)) return FALSE;  /* pattern too large */
  memset(seen, 0, code_len + 1);
  if (!first_set_walk(code, code_len, classes, 0, bm, seen))
    return FALSE;
  /* Check if bitmap is all-ones (no benefit to skip) */
  int set_bits = 0;
  for (int i = 0; i < 16; i++) {
    for (int b = 0; b < 8; b++) {
      if (bm[i] & (1 << b)) set_bits++;
    }
  }
  return set_bits < 96;  /* useful only if fewer than 75% of bytes match */
}

mrb_regexp_pattern*
re_compile(const char *pattern, mrb_int len, uint32_t flags)
{
  const char *orig_pattern = pattern;
  mrb_int orig_len = len;
  re_compiler c;
  memset(&c, 0, sizeof(c));

  if (flags & RE_FLAG_EXTENDED) {
    mrb_int slen;
    c.stripped = strip_extended(pattern, len, &slen);
    pattern = c.stripped;
    len = slen;
  }
  c.src = pattern;
  c.src_end = pattern + len;
  c.p = pattern;
  c.flags = flags;
  c.num_captures = 1;  /* group 0 = whole match */

  /* group 0 start */
  emit(&c, RE_SAVE, 0, 0);

  compile_alt(&c);

  if (c.p < c.src_end) {
    compile_error(&c, "unmatched ')'");
  }

  /* group 0 end */
  emit(&c, RE_SAVE, 0, 1);
  emit(&c, RE_MATCH, 0, 0);

  mrb_regexp_pattern *pat = (mrb_regexp_pattern*)malloc(sizeof(mrb_regexp_pattern));
  pat->code = c.code;
  pat->code_len = c.code_len;
  pat->classes = c.classes;
  pat->num_classes = c.num_classes;
  pat->num_captures = c.num_captures;
  pat->flags = flags;
  pat->named_captures = c.named_captures;
  pat->num_named = c.num_named;
  pat->has_backref = c.has_backref;
  pat->needs_backtrack = c.needs_backtrack;

  /* Extract literal prefix for fast search skip.
     Walk bytecode from the start, skipping SAVE, collecting RE_CHAR. */
  {
    uint8_t pbuf[256];
    int plen = 0;
    for (uint32_t i = 0; i < pat->code_len && plen < 255; i++) {
      if (pat->code[i].op == RE_SAVE) continue;
      if (pat->code[i].op == RE_CHAR) {
        pbuf[plen++] = pat->code[i].a;
      }
      else break;
    }
    if (plen > 0) {
      pat->prefix = (uint8_t*)malloc(plen);
      memcpy(pat->prefix, pbuf, plen);
      pat->prefix_len = (uint8_t)plen;
    }
    else {
      pat->prefix = NULL;
      pat->prefix_len = 0;
    }
  }

  /* Check if pattern is pure literal: SAVE CHAR* SAVE MATCH only.
     prefix_len already holds the literal char count if so. */
  pat->is_literal = FALSE;
  if (pat->prefix_len > 0 && pat->num_captures == 1 &&
      !pat->has_backref && !pat->needs_backtrack) {
    /* bytecode should be: SAVE(0), CHAR*N, SAVE(1), MATCH
       = 2 + prefix_len + 2 = prefix_len + 2 instructions
       (SAVE(0) at 0, CHARs at 1..N, SAVE(1) at N+1, MATCH at N+2) */
    if (pat->code_len == (uint32_t)(pat->prefix_len + 3) &&
        pat->code[0].op == RE_SAVE &&
        pat->code[pat->code_len - 2].op == RE_SAVE &&
        pat->code[pat->code_len - 1].op == RE_MATCH) {
      pat->is_literal = TRUE;
    }
  }

  /* Compute first-byte bitmap: set of bytes that could start a match.
     Used when prefix is empty (e.g. alternation, character class patterns). */
  {
    uint8_t bm[16];
    memset(bm, 0, sizeof(bm));
    pat->has_first_bytes = compute_first_set(pat->code, pat->code_len, pat->classes, bm);
    if (pat->has_first_bytes) {
      memcpy(pat->first_bytes, bm, 16);
    }
  }

  /* Pre-allocate VM state cache for pike_vm */
  {
    int list_capa = (int)pat->code_len * 2 + 16;
    pat->cached_visited = (uint32_t*)calloc(pat->code_len + 1, sizeof(uint32_t));
    pat->cached_threads[0] = malloc(sizeof(re_thread_cache) * list_capa);
    pat->cached_threads[1] = malloc(sizeof(re_thread_cache) * list_capa);
    pat->cached_list_capa = list_capa;
    pat->cache_in_use = FALSE;
  }

  if (c.stripped) free(c.stripped);
  pat->source = (char *)malloc((size_t)orig_len + 1);
  if (pat->source) { memcpy(pat->source, orig_pattern, (size_t)orig_len); pat->source[orig_len] = 0; }
  return pat;
}

void
re_free(mrb_regexp_pattern *pat)
{
  if (pat && pat->source) { free(pat->source); pat->source = NULL; }
  if (pat) {
    free(pat->code);
    if (pat->classes) {
      for (uint16_t i = 0; i < pat->num_classes; i++) {
        free(pat->classes[i].ranges);
      }
      free(pat->classes);
    }
    /* Issue #823: per-name buffers now owned by the table; free each
       before freeing the array itself. Names registered before the
       fix were uninit memory pointers but the new allocator path
       gives us our own copy. */
    if (pat->named_captures) {
      for (uint16_t i = 0; i < pat->num_named; i++) {
        free((void *)pat->named_captures[i].name);
      }
      free(pat->named_captures);
    }
    free(pat->prefix);
    free(pat->cached_visited);
    free(pat->cached_threads[0]);
    free(pat->cached_threads[1]);
    free(pat);
  }
}

/* ---- named-capture introspection (engine ABI) ----
   The compiled pattern retains every `(?<name>...)` group's name and 1-based
   group index; these expose that table so the MatchData layer can resolve a
   name to a capture group without reaching into the internal struct. */
int re_num_named(const mrb_regexp_pattern *pat) {
  return pat ? (int)pat->num_named : 0;
}
const char *re_named_name(const mrb_regexp_pattern *pat, int i, int *group_out) {
  if (!pat || i < 0 || i >= (int)pat->num_named) return NULL;
  if (group_out) *group_out = (int)pat->named_captures[i].group;
  return pat->named_captures[i].name;
}
/* group index -> its name (NUL-terminated copy in a static rotating buffer),
   or NULL when the group is positional. For MatchData#inspect. */
const char *re_group_name(const mrb_regexp_pattern *pat, int group) {
  static char buf[4][64];
  static int rot = 0;
  if (!pat) return NULL;
  for (int k = 0; k < pat->num_named; k++) {
    if (pat->named_captures[k].group == group) {
      int n = pat->named_captures[k].name_len;
      if (n > 63) n = 63;
      char *o = buf[rot = (rot + 1) & 3];
      memcpy(o, pat->named_captures[k].name, (size_t)n);
      o[n] = 0;
      return o;
    }
  }
  return NULL;
}

int re_named_group(const mrb_regexp_pattern *pat, const char *name) {
  if (!pat || !name) return -1;
  size_t nlen = strlen(name);
  int group = -1;
  /* A name may repeat across alternation branches; CRuby resolves to the last
     declared group, so keep scanning and take the final match. */
  for (uint16_t i = 0; i < pat->num_named; i++) {
    const re_named_capture *nc = &pat->named_captures[i];
    /* compare name_len (uint16_t, promoted to size_t) against nlen directly: a
       name >= 65536 chars must never spuriously match a truncated 16-bit length
       and then memcmp past the stored name. */
    if (nc->name_len == nlen && memcmp(nc->name, name, nlen) == 0)
      group = (int)nc->group;
  }
  return group;
}

/* sp_sprintf lives in the generated TU (same late-bind as sp_re.c uses) */
extern const char *sp_sprintf(const char *fmt, ...);

/* Regexp rendering from the retained pattern text: #source is the raw text,
   #inspect is /src/flags, #to_s is CRuby's (?on-off:src) with the m/i/x set. */
const char *sp_re_source(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  return (pat && pat->source) ? pat->source : "";
}
const char *sp_re_inspect_str(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  char fl[4]; int n = 0;
  uint32_t f = pat ? pat->flags : 0;
  if (f & RE_FLAG_DOTALL) fl[n++] = 'm';
  if (f & RE_FLAG_IGNORECASE) fl[n++] = 'i';
  if (f & RE_FLAG_EXTENDED) fl[n++] = 'x';
  fl[n] = 0;
  return sp_sprintf("/%s/%s", sp_re_source(pat), fl);
}
const char *sp_re_to_s_str(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  char on[4], off[4]; int no = 0, nf = 0;
  uint32_t f = pat ? pat->flags : 0;
  if (f & RE_FLAG_DOTALL) on[no++] = 'm'; else off[nf++] = 'm';
  if (f & RE_FLAG_IGNORECASE) on[no++] = 'i'; else off[nf++] = 'i';
  if (f & RE_FLAG_EXTENDED) on[no++] = 'x'; else off[nf++] = 'x';
  on[no] = 0; off[nf] = 0;
  if (nf) return sp_sprintf("(?%s-%s:%s)", on, off, sp_re_source(pat));
  return sp_sprintf("(?%s:%s)", on, sp_re_source(pat));
}
/* Regexp#options: CRuby's public option bits IGNORECASE=1, EXTENDED=2,
   MULTILINE=4 (the /m "dot matches newline", our internal DOTALL). The
   internal RE_FLAG_MULTILINE (^/$ at line ends) is not a Ruby-visible option. */
mrb_int sp_re_options(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  uint32_t f = pat ? pat->flags : 0;
  mrb_int o = 0;
  if (f & RE_FLAG_IGNORECASE) o |= 1;
  if (f & RE_FLAG_EXTENDED)   o |= 2;
  if (f & RE_FLAG_DOTALL)     o |= 4;
  return o;
}
mrb_bool sp_re_casefold_p(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  return (pat && (pat->flags & RE_FLAG_IGNORECASE)) ? TRUE : FALSE;
}
/* The engine's own internal flag word, for re-compiling a copy (Regexp.new(re))
   with the source pattern's exact options preserved. */
uint32_t sp_re_raw_flags(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  return pat ? pat->flags : 0;
}

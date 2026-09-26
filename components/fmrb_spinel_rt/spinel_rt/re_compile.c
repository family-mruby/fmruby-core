/*
** re_compile.c - regexp pattern compiler
**
** Compiles a regular expression pattern string into bytecode
** for the NFA execution engine.
**
** See Copyright Notice in mruby.h
*/

#include "re_internal.h"
#include <stdbool.h>
/* mruby header removed */
#include <string.h>

/* Compiler state */
typedef struct re_compiler_s re_compiler;
static void emit_atom_copy(re_compiler *c, uint32_t start, uint32_t size);

struct re_compiler_s {
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
  uint32_t orig_flags;  /* the flags re_compile() was called with, for error
                           messages: unlike `flags`, an inline option such as
                           `(?i)` does not change this */
  re_named_capture *named_captures;
  uint16_t num_named;
  mrb_bool has_backref;
  mrb_bool needs_backtrack;
  /* Ruby stops capturing plain `(...)` groups once the pattern names one, so
     the whole source is scanned for a name before compiling (#3678). */
  mrb_bool has_named_group;
  /* the atom just read was a (?#...) comment: it consumes source but is not a
     repeatable atom, so a quantifier after it is CRuby's "no target" error,
     unlike the empty group `(?:)` which repeats to an empty match */
  mrb_bool last_atom_comment;
  /* where the atom a quantifier binds to begins: compile_quantified sets it to
     the code position before the atom, and a `\u{...}` list moves it forward so
     the quantifier repeats the last codepoint alone */
  uint32_t atom_start;
  char *stripped;           /* allocated buffer for x-mode preprocessing */
  /* Code range of each capture group, so `\g<name>` can re-emit a copy of the
     group it calls (#3637). Indexed by group number; len 0 = not (yet) known. */
  uint32_t grp_start[RE_MAX_CAPTURES];
  uint32_t grp_len[RE_MAX_CAPTURES];
};

/* Does the pattern source declare a named group -- `(?<name>` or `(?'name'`,
   as opposed to the lookbehinds `(?<=` / `(?<!`? Character classes are skipped
   so a bracketed `(` cannot open a group (#3678). */
static mrb_bool re_src_has_named_group(const char *p, const char *end) {
  int in_class = 0;
  for (; p < end; p++) {
    if (*p == '\\') { p++; continue; }
    if (in_class) { if (*p == ']') in_class = 0; continue; }
    if (*p == '[') { in_class = 1; continue; }
    if (*p != '(' || p + 2 >= end || p[1] != '?') continue;
    if (p[2] == '\'') return TRUE;
    if (p[2] == '<' && p + 3 < end && p[3] != '=' && p[3] != '!') return TRUE;
  }
  return FALSE;
}

static void compile_alt(re_compiler *c);  /* forward */
static void emit_codepoint(re_compiler *c, uint32_t cp);  /* forward */
static mrb_bool emit_cp_folded(re_compiler *c, uint32_t cp);  /* forward */

/* Issue #781: error handler hook so the library can route through
   the user program's sp_raise_cls (which is `static inline` per
   translation unit and not directly linkable from a .a). The user
   program calls sp_re_set_error_handler(fn) at startup; fn should
   not return (typically wraps sp_raise_cls). If unset, fall back
   to fprintf + exit. */
#ifdef SP_MULTI_CTX
/* Per instance: each program installs its own handler, which raises into its
   own exception stack. This engine cannot include sp_ctx.h (its types clash
   with the runtime's), so the slot is reached through sp_ctx.c. */
typedef void (*sp_re_error_fn)(const char *msg);
sp_re_error_fn *sp_ctx_re_error_handler(void);
#define sp_re_error_handler (*sp_ctx_re_error_handler())
#else
static void (*sp_re_error_handler)(const char *msg) = NULL;
#endif
void sp_re_set_error_handler(void (*fn)(const char *msg)) {
  sp_re_error_handler = fn;
}

static __attribute__((noreturn)) void compile_error(re_compiler *c, const char *msg);

/* Write the set flags among m/i/x into `buf` as letters, in the order
   Regexp#to_s and Regexp#inspect write them, and NUL-terminate. `buf` holds
   4 bytes. Shared so a compile error can quote a pattern the way
   Regexp#inspect would, `/pattern/flags`, without a second copy of the order.
   Regexp#to_s keeps its own loop: it also tracks the flags that are off,
   which this does not need. (ported from mruby-regexp a23691440) */
#define RE_FLAGS_STR_SIZE 4
static void
re_flags_cat(char *buf, uint32_t flags)
{
  int n = 0;
  if (flags & RE_FLAG_DOTALL) buf[n++] = 'm';
  if (flags & RE_FLAG_IGNORECASE) buf[n++] = 'i';
  if (flags & RE_FLAG_EXTENDED) buf[n++] = 'x';
  buf[n] = 0;
}

/* CRuby's `invalid group name <...>` quotes the name it could not read.
   Upstream spells the slice through mrb_format's %l; compile_error here takes
   a plain string, so the quoting happens first. */
static __attribute__((noreturn)) void
compile_error_group_name(re_compiler *c, const char *name, size_t len)
{
  char buf[512];
  snprintf(buf, sizeof(buf), "invalid group name <%.*s>", (int)len, name);
  compile_error(c, buf);
}

static __attribute__((noreturn)) void
compile_error(re_compiler *c, const char *msg)
{
  /* Build the message before freeing: in /x (extended) mode c->src aliases
     c->stripped, so reading it after the free would be a use-after-free. */
  /* The suffix uses c->orig_flags, not c->flags: an inline option such as
     `(?i)` mutates the latter partway through the parse, where CRuby's
     message (and Regexp#inspect) reports the flags the pattern was compiled
     WITH. So `Regexp.new("(?-x)a # c[", Regexp::EXTENDED)` still reports /x.
     Quoting the flags is what lets the message be pasted back into
     Regexp.new to reproduce the failure. (ported from mruby-regexp
     24fb9124a) */
  char fl[RE_FLAGS_STR_SIZE];
  re_flags_cat(fl, c->orig_flags);
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s: /%.*s/%s",
           msg, (int)(c->src_end - c->src), c->src, fl);
  /* Free all half-built compiler state: a catchable RegexpError handler
     longjmps out and `c` never returns to re_compile, so the bytecode,
     char-class ranges, and named-capture names would otherwise leak. (On
     success these transfer to the compiled pattern instead.) */
  free(c->code);
  c->code = NULL;
  if (c->classes) {
    for (uint16_t i = 0; i < c->num_classes; i++) {
      free(c->classes[i].ranges);
#ifdef RE_UNICODE_CTYPE
      free(c->classes[i].props);
#endif
    }
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
    /* The atomic group carries its jump-to-end target in `offset` as well.
       Left out of this walk, `(?>a?)*` kept the target the atom had before the
       quantifier's SPLIT was inserted ahead of it -- one short, which is the
       sub-pattern's own MATCH: the match ended there and the outer SAVE that
       records where group 0 ends never ran, so `m[0]` was nil. */
    case RE_ATOMIC:
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

static void class_add_codepoint(re_charclass *cc, uint32_t cp);  /* forward */

/* Add every case counterpart of the class's codepoint members. The members are
   ranges, so the walk is over the range list as it stood on entry (the count is
   read once, since the counterparts are appended to the same list), and each
   range is intersected with the folding runs rather than enumerated: a member
   of a wide range that carries no folding costs nothing.
   (ported from mruby-regexp 618ba9435) */
static void
class_fold_codepoints(re_compiler *c, re_charclass *cc)
{
  (void)c;
  uint32_t n = cc->num_ranges;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t lo = cc->ranges[2 * i], hi = cc->ranges[2 * i + 1];
    /* A range wider than this is a bulk range (`[^\x00-\x{10FFFF}]`-shaped);
       its counterparts are inside it already. */
    if (hi - lo > 0x1000) continue;
    for (uint32_t cp = lo; cp <= hi; cp++) {
      uint32_t alts[RE_CASE_ALTS_MAX];
      int na = re_case_alts(cp, alts);
      for (int k = 0; k < na; k++) {
        if (alts[k] == cp) continue;
        if (alts[k] < 128) class_set_bit(cc, (uint8_t)alts[k]);
        else class_add_codepoint(cc, alts[k]);
      }
      if (cp == hi) break;  /* cp is unsigned: hi == UINT32_MAX would wrap */
    }
  }
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
#ifdef RE_UNICODE_CTYPE
/* Record a `\p{...}` property on a class. Returns FALSE when the engine does
   not carry the property, so the caller can name it in the error. */
static mrb_bool
class_add_prop(re_charclass *cc, int id, mrb_bool negated)
{
  if (cc->num_props >= cc->prop_capa) {
    int capa = cc->prop_capa ? cc->prop_capa * 2 : 4;
    void *p = realloc(cc->props, sizeof(*cc->props) * (size_t)capa);
    if (!p) return FALSE;
    cc->props = p;
    cc->prop_capa = capa;
  }
  cc->props[cc->num_props].id = (int16_t)id;
  cc->props[cc->num_props].negated = negated;
  cc->num_props++;
  return TRUE;
}
#endif

static mrb_bool
class_add_posix(re_charclass *cc, const char *name, size_t len, uint16_t *ctype)
{
#define POSIX_IS(s) (len == sizeof(s) - 1 && memcmp(name, s, len) == 0)
/* What the name holds ABOVE ASCII, as the re_ctype bit a build carrying
   re_ctype.h reads off the table at match time. [:xdigit:] and [:ascii:] are
   sets ASCII defines, so they hold nothing above it and take no bit; without
   the table every bracket is such a set and nothing is written here.
   (ported from mruby-regexp 55b6deab4) */
#ifdef RE_UNICODE_CTYPE
#define TYPE(t) (*ctype = (t))
#else
#define TYPE(t) ((void)0)
#endif
  *ctype = 0;
  if (POSIX_IS("alpha")) {
    class_set_range(cc, 'a', 'z');
    class_set_range(cc, 'A', 'Z');
    TYPE(RE_CTYPE_ALPHA);
  }
  else if (POSIX_IS("digit")) {
    class_set_range(cc, '0', '9');
    TYPE(RE_CTYPE_DIGIT);
  }
  else if (POSIX_IS("alnum")) {
    class_set_range(cc, 'a', 'z');
    class_set_range(cc, 'A', 'Z');
    class_set_range(cc, '0', '9');
    TYPE(RE_CTYPE_ALNUM);
  }
  else if (POSIX_IS("upper")) {
    class_set_range(cc, 'A', 'Z');
    TYPE(RE_CTYPE_UPPER);
  }
  else if (POSIX_IS("lower")) {
    class_set_range(cc, 'a', 'z');
    TYPE(RE_CTYPE_LOWER);
  }
  else if (POSIX_IS("space")) {
    /* [ \t\n\v\f\r] */
    class_set_range(cc, '\t', '\r');
    class_set_bit(cc, ' ');
    TYPE(RE_CTYPE_SPACE);
  }
  else if (POSIX_IS("blank")) {
    class_set_bit(cc, ' ');
    class_set_bit(cc, '\t');
    TYPE(RE_CTYPE_BLANK);
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
    TYPE(RE_CTYPE_WORD);
  }
  else if (POSIX_IS("cntrl")) {
    class_set_range(cc, 0, 0x1f);
    class_set_bit(cc, 0x7f);
    TYPE(RE_CTYPE_CNTRL);
  }
  else if (POSIX_IS("print")) {
    /* printable, including space: 0x20-0x7e */
    class_set_range(cc, 0x20, 0x7e);
    TYPE(RE_CTYPE_PRINT);
  }
  else if (POSIX_IS("graph")) {
    /* printable, excluding space: 0x21-0x7e */
    class_set_range(cc, 0x21, 0x7e);
    TYPE(RE_CTYPE_GRAPH);
  }
  else if (POSIX_IS("punct")) {
    /* printable non-alnum non-space ASCII */
    class_set_range(cc, '!', '/');
    class_set_range(cc, ':', '@');
    class_set_range(cc, '[', '`');
    class_set_range(cc, '{', '~');
    TYPE(RE_CTYPE_PUNCT);
  }
  else if (POSIX_IS("ascii")) {
    class_set_range(cc, 0, 0x7f);
  }
  else {
    return FALSE;
  }
  return TRUE;
#undef POSIX_IS
#undef TYPE
}

/* Negated POSIX class `[:^name:]`: add the ASCII complement of the named set.
   Above ASCII the type is read off the table at match time in either
   polarity, so the negation is the ctype_no the caller records rather than a
   blanket utf8_any -- that is what made `"aあx" =~ /[[:^alpha:]]x/` answer 1
   where CRuby answers nil. A set ASCII defines holds nothing above ASCII, so
   its negation holds everything there and utf8_any is right for it; without
   the table every bracket is such a set. Returns FALSE for an unrecognized
   name so the caller can raise. */
/* class_add_posix_negated is defined below; class_read_property needs both. */
static mrb_bool class_add_posix_negated(re_charclass *cc, const char *name,
                                        size_t len, uint16_t *ctype);

#ifdef RE_UNICODE_CTYPE
/* Parse a `\p{Name}` / `\P{Name}` at `*pp` (which points at the `p`), record
   it on `cc`, and advance past the closing brace. `esc_neg` is TRUE for the
   capital spelling. Returns FALSE, with the name copied into `errname`, when
   the engine does not carry that property -- the caller names it in the error
   rather than saying only that properties are unsupported, which is what tells
   an author whether to reach for another spelling or to file.

   A POSIX-named property is the type re_ctype.h already carries, and routing
   it there rather than to a table of its own is what makes `\p{Alpha}` fold
   under /i exactly as `[[:alpha:]]` does. */
static mrb_bool
class_read_property(re_charclass *cc, const char **pp, const char *end,
                    mrb_bool esc_neg, char *errname, size_t errcap)
{
  const char *p = *pp + 1;          /* past the p/P */
  if (p >= end || *p != '{') return FALSE;
  p++;
  mrb_bool negated = esc_neg;
  if (p < end && *p == '^') { negated = !negated; p++; }
  const char *name = p;
  while (p < end && *p != '}') p++;
  if (p >= end) return FALSE;       /* unterminated: not a property */
  size_t len = (size_t)(p - name);
  if (errcap) {
    size_t n = len < errcap - 1 ? len : errcap - 1;
    memcpy(errname, name, n);
    errname[n] = 0;
  }
  char lower[32];
  if (len > 0 && len < sizeof(lower)) {
    for (size_t k = 0; k < len; k++) {
      char ch = name[k];
      lower[k] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
    }
    lower[len] = 0;
    uint16_t ctype = 0;
    mrb_bool ok = negated ? class_add_posix_negated(cc, lower, len, &ctype)
                          : class_add_posix(cc, lower, len, &ctype);
    if (ok) {
      if (ctype) {
        if (negated) cc->ctype_no |= ctype;
        else cc->ctype_yes |= ctype;
      }
      *pp = p + 1;
      return TRUE;
    }
  }
  int id = re_prop_lookup(name, len);
  if (id < 0) return FALSE;
  if (!class_add_prop(cc, id, negated)) return FALSE;
  /* ASCII is answered from the class bitmap and never reaches the property
     list -- the same split the POSIX brackets use, which write their ASCII
     members out at compile time and read the table only above it. Writing
     them here costs 128 lookups once and leaves the match path alone. */
  for (uint32_t a = 0; a < 128; a++) {
    mrb_bool has = re_prop_match(id, a);
    if (negated ? !has : has) class_set_bit(cc, (uint8_t)a);
  }
  *pp = p + 1;
  return TRUE;
}
#endif

static mrb_bool
class_add_posix_negated(re_charclass *cc, const char *name, size_t len,
                        uint16_t *ctype)
{
  re_charclass tmp;
  memset(&tmp, 0, sizeof(tmp));
  if (!class_add_posix(&tmp, name, len, ctype)) return FALSE;
  for (int i = 0; i < 128; i++) {
    if (!(tmp.bitmap[i >> 3] & (1 << (i & 7)))) class_set_bit(cc, (uint8_t)i);
  }
  if (*ctype == 0) cc->utf8_any = TRUE;
  return TRUE;
}

static int parse_escape(re_compiler *c);

/* Read the character a control escape names and return the control character
   it stands for. `\cX` and `\C-X` name the same one, and a `\` in the X
   position opens an escape of its own, so `\c\n` is a control newline.

   The mask is the one this build's own lexer uses for a string, which is what
   a regexp literal is read by: /\cA/ reaches the engine as the byte already.
   Only Regexp.new() with a written-out backslash arrives here, and the two
   spellings have to name the same character. That settles `\c?`, where CRuby
   disagrees with itself: its lexer answers DEL and Onig answers 0x1f, so
   /\c?/ and Regexp.new("\\c?") are two different patterns there. Following
   the lexer keeps the pair together here. */
static int
parse_control_escape(re_compiler *c)
{
  int ch = next_char(c);

  if (ch < 0) compile_error(c, "too short control escape");
  if (ch == '\\') ch = parse_escape(c);
  if (ch == '?') return 0x7f;
  return ch & 0x1f;
}

static int
parse_escape(re_compiler *c)
{
  int ch = next_char(c);
  if (ch < 0) compile_error(c, "trailing backslash");
  switch (ch) {
  case 'c':
    return parse_control_escape(c);
  case 'C':
    /* Only the `\C-X` spelling names a control character; a `\C` with
       anything else after it is the escape ending early, as it is to CRuby. */
    if (peek(c) != '-') compile_error(c, "too short control escape");
    next_char(c);
    return parse_control_escape(c);
  case 'M':
    /* `\M-X` sets the high bit, making a byte that starts no character. This
       engine has no encoding to read one against, and CRuby refuses the escape
       in a pattern that is not binary, so it is refused rather than answered
       with a byte nothing matches. */
    compile_error(c, "meta escape is not supported");
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

/* Reject what has no UTF-8 encoding. CRuby reports both a surrogate and a
   value past the last plane as "invalid Unicode range", so neither ever
   reaches re_utf8_encode(). (ported from mruby-regexp 048e5da5f) */
static void
check_unicode_cp(re_compiler *c, uint32_t cp)
{
  if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
    compile_error(c, "invalid Unicode range");
  }
}

/* Separator between the codepoints of a `\u{...}` list. */
static mrb_bool
unicode_list_space(int ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' || ch == '\r';
}

static int hex_digit_value(int ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

/* Read the next codepoint of an open `\u{...}` list, or close it. Returns
   FALSE once the `}` is consumed, leaving *more FALSE so the caller's loop
   ends. */
static mrb_bool
unicode_escape_next(re_compiler *c, mrb_bool *more, uint32_t *out)
{
  if (!*more) return FALSE;
  while (unicode_list_space(peek(c))) next_char(c);
  if (peek(c) == '}') {
    next_char(c);
    *more = FALSE;
    return FALSE;
  }

  uint32_t cp = 0;
  int n = 0;
  for (;;) {
    int v = hex_digit_value(peek(c));
    if (v < 0) break;
    next_char(c);
    cp = cp * 16 + (uint32_t)v;
    /* Six digits reach U+FFFFFF, past the last plane, so a seventh can only
       be an overlong spelling. CRuby rejects `\u{0000061}` rather than
       reading it as U+0061. */
    if (++n > 6) compile_error(c, "invalid Unicode range");
  }
  /* Anything that is neither a hex digit nor the closing brace ends the
     list: a separator CRuby does not take (`\u{61,62}`), or the end of the
     pattern (`\u{61`). */
  if (n == 0) compile_error(c, "invalid Unicode list");
  check_unicode_cp(c, cp);
  *out = cp;
  return TRUE;
}

/* Read a `\u` escape and return its first codepoint; the backslash and the
   `u` are already consumed. `\uXXXX` is exactly four hex digits and yields
   one codepoint. `\u{...}` holds one or more, so *more is set and the rest
   come from unicode_escape_next(). */
static uint32_t
unicode_escape_first(re_compiler *c, mrb_bool *more)
{
  *more = FALSE;
  if (peek(c) == '{') {
    next_char(c);
    *more = TRUE;
    uint32_t cp;
    /* The list has to hold something: `\u{}` and `\u{ }` are errors, not an
       escape that contributes nothing. */
    if (!unicode_escape_next(c, more, &cp)) compile_error(c, "invalid Unicode list");
    return cp;
  }

  /* Nothing at all after `\u` is reported apart from a bad digit, as CRuby
     does: /\u/ is "too short escape sequence" while /\u6/ is not. */
  if (peek(c) < 0) compile_error(c, "too short escape sequence");
  uint32_t cp = 0;
  for (int i = 0; i < 4; i++) {
    int v = hex_digit_value(peek(c));
    if (v < 0) compile_error(c, "invalid Unicode escape");
    next_char(c);
    cp = cp * 16 + (uint32_t)v;
  }
  check_unicode_cp(c, cp);
  return cp;
}

/* Read one character class atom: either an ASCII byte (0-127), a
   `\escape`, or a full multi-byte UTF-8 codepoint. Returns the
   codepoint and advances c->p. */
static uint32_t
read_class_atom(re_compiler *c, re_charclass *cc)
{
  if (peek(c) == '\\') {
    next_char(c);
    if (peek(c) == 'u') {
      next_char(c);
      mrb_bool more;
      uint32_t cp = unicode_escape_first(c, &more);
      uint32_t nx;
      /* Every codepoint of a `\u{...}` list is a member of its own. All but
         the last join the class here; the last is returned, so it can open a
         range as any other atom would: `[\u{61 62}-z]` is `a` plus `b-z`. */
      while (unicode_escape_next(c, &more, &nx)) {
        if (cp < 128) class_set_bit(cc, (uint8_t)cp);
        else class_add_codepoint(cc, cp);
        cp = nx;
      }
      return cp;
    }
    /* Unlike `\K`/`\R`/`\X`, CRuby reads a property escape inside a class as
       a property test there too, not as its literal letters -- `[\p{Alpha}]`
       matches letters, it doesn't hold the text "p{Alpha}". */
    /* Handled by the class loop before it reads an atom -- a property is a
       set, so it is not an atom and cannot open a range. Reaching here means
       the loop did not recognise it. */
    if ((peek(c) == 'p' || peek(c) == 'P') && c->p + 1 < c->src_end && c->p[1] == '{') {
      compile_error(c, "character property is not supported");
    }
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

/* Whether `\X` at `src` is one of the shorthand classes the class parser
   folds in whole. Each names a set rather than a character. */
static mrb_bool
at_shorthand_class(const char *src, const char *end)
{
  if (*src != '\\' || src + 1 >= end) return FALSE;
  switch (src[1]) {
  case 'd': case 'D': case 'w': case 'W':
  case 's': case 'S': case 'h': case 'H':
    return TRUE;
  default:
    return FALSE;
  }
}

/* A shorthand and a POSIX bracket each name a set, so neither can be an end
   of a range. Called where one has just been folded in: a '-' after it opens
   a range this class cannot have, and CRuby reports that '-' rather than
   reading it as a member. A '-' just before the ']' is a member in both, so
   `[\w-]` still holds one. (ported from mruby-regexp 06ed794af) */
static void
reject_set_as_range_start(re_compiler *c)
{
  if (peek(c) == '-' && c->p + 1 < c->src_end && c->p[1] != ']') {
    compile_error(c, "unmatched range specifier in char-class");
  }
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
        reject_set_as_range_start(c);
        continue;
      }
    }

    /* POSIX bracket class `[:name:]` inside the enclosing `[...]`. Adds
       the corresponding ASCII ranges via the same helpers used for `\d`
       etc.; enclosing negation is applied by the RE_NCLASS emit below. A
       leading `^` (`[:^name:]`) negates the class in place. A complete
       `[:...:]` with an unrecognized name is a hard error in CRuby
       ("invalid POSIX bracket type"); raise rather than match nothing. */
    /* A '[' inside a class opens something in CRuby rather than standing for
       itself: a POSIX bracket, a collating element, an equivalence class, or a
       class nested in this one. Only the bracket is read here and the rest are
       refused, since taken as members they compile to a different pattern than
       the one written: [[a][b]] is the union of two classes there and was `[`
       or `a`, then b, then `]` here. `[\[]` holds the bracket itself, in CRuby
       as well. A '[' with nothing after it leaves the class unterminated,
       which the loop reports on its own. */
    if (peek(c) == '[' && c->p + 1 < c->src_end) {
      if (c->p[1] == '.') compile_error(c, "POSIX collating element is not supported");
      if (c->p[1] == '=') compile_error(c, "POSIX equivalence class is not supported");
      if (c->p[1] != ':') compile_error(c, "nested character class is not supported");
    }

    if (peek(c) == '[' && c->p + 1 < c->src_end && c->p[1] == ':') {
      const char *name = c->p + 2;
      mrb_bool posix_neg = FALSE;
      if (name < c->src_end && *name == '^') { posix_neg = TRUE; name++; }
      const char *q = name;
      while (q < c->src_end && *q != ':' && *q != ']') q++;
      if (q + 1 < c->src_end && q[0] == ':' && q[1] == ']') {
        size_t nlen = (size_t)(q - name);
        uint16_t ctype = 0;
        mrb_bool ok = posix_neg ? class_add_posix_negated(cc, name, nlen, &ctype)
                                : class_add_posix(cc, name, nlen, &ctype);
        if (!ok) compile_error(c, "invalid POSIX bracket type");
#ifdef RE_UNICODE_CTYPE
        /* Above ASCII the bracket is a type read at match time, not members
           written out: a class holding [[:alpha:]] would otherwise carry the
           letters as hundreds of ranges and walk them at every character. */
        if (ctype) {
          if (posix_neg) cc->ctype_no |= ctype;
          else cc->ctype_yes |= ctype;
        }
#endif
        c->p = q + 2;  /* consume past ":]" */
        reject_set_as_range_start(c);
        continue;
      }
      /* The '[' opened a bracket, so a name that does not close is the
         bracket ending early rather than a literal '[', as it is to CRuby.
         Which of the two things went wrong depends on where the scan stopped:
         at a ']' the class does close and only the bracket ended early, and
         anywhere else (a ':' with nothing after it, or the end of the pattern)
         the class never closes either, which is the older and more particular
         complaint of the two. */
      compile_error(c, (q < c->src_end && *q == ']')
                    ? "premature end of char-class"
                    : "unterminated character class");
    }

#ifdef RE_UNICODE_CTYPE
    /* `[\p{Lu}\p{Nd}]`: a property inside a class is a set, like a POSIX
       bracket, so it joins the class rather than being read as an atom --
       and, like one, it cannot be an endpoint of a range. */
    if (peek(c) == '\\' && c->p + 2 < c->src_end &&
        (c->p[1] == 'p' || c->p[1] == 'P') && c->p[2] == '{') {
      char pname[64];
      const char *pp = c->p + 1;
      if (class_read_property(cc, &pp, c->src_end, c->p[1] == 'P',
                              pname, sizeof(pname))) {
        c->p = pp;
        reject_set_as_range_start(c);
        continue;
      }
      { char ebuf[128];
        snprintf(ebuf, sizeof(ebuf), "character property is not supported: %s", pname);
        compile_error(c, ebuf); }
    }
#endif

    uint32_t cp = read_class_atom(c, cc);

    /* check for range a-z (or U+xxxx-U+yyyy) */
    if (peek(c) == '-' && c->p + 1 < c->src_end && c->p[1] != ']') {
      next_char(c);  /* skip '-' */
      /* A set cannot close a range either. Read as a character `\d` is the
         letter, so [a-\d] was [a-d], a class of four letters rather than the
         error CRuby reports. A POSIX bracket in that place is caught by the
         backwards-range check below, since its '[' sorts under every letter. */
      if (at_shorthand_class(c->p, c->src_end)) {
        compile_error(c, "char-class value at end of range");
      }
      uint32_t hi = read_class_atom(c, cc);
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
    /* The same for the codepoint members, which the bitmap does not cover:
       every counterpart of a member is a member too. The ranges are walked
       over the FOLD runs rather than over their own length, so a wide range
       costs the run count. (ported from mruby-regexp 618ba9435) */
    class_fold_codepoints(c, cc);
  }

#ifdef RE_UNICODE_CTYPE
  /* A type is not spelled out as members, so the /i closure over the class's
     members never saw it; it is closed at match time instead, by reading the
     type of every character sharing the folding of the one in hand. */
  cc->ctype_fold = (c->flags & RE_FLAG_IGNORECASE) &&
                   (cc->ctype_yes || cc->ctype_no);
#endif

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
/* TRUE when every character the class can match is ASCII, so it always
   consumes exactly one byte. Non-ASCII codepoint ranges and the utf8_any
   catch-all (set by \D, \W, \S, \H and [[:^ascii:]]) both admit multibyte
   characters, whose width is not known until match time, and so does a POSIX
   bracket, which holds whatever the type table says above ASCII. (ported from
   mruby-regexp 1fa7d26c4) */
static mrb_bool
class_is_ascii_only(const re_charclass *cc)
{
#ifdef RE_UNICODE_CTYPE
  if (cc->ctype_yes || cc->ctype_no) return FALSE;
#endif
  return cc->num_ranges == 0 && !cc->utf8_any;
}

static int
compute_fixed_len(re_compiler *c, uint32_t start, uint32_t end, int *chars_out)
{
  int len = 0;
  int chars = 0;
  uint32_t pc = start;

  while (pc < end) {
    re_inst inst = c->code[pc];
    switch (inst.op) {
    case RE_CHAR: {
      /* A multibyte literal is a run of one-byte RE_CHAR instructions, and
         what a byte spells depends on the bytes after it, so hand the run to
         the character measurer rather than read the lead bit alone: a
         continuation byte no lead reaches is a character of its own, which is
         the rule the executor rewinds by. Four bytes is the longest character
         there is, and a run never splits one. */
      char buf[4];
      int n = 0;
      while (n < 4 && pc + (uint32_t)n < end && c->code[pc + n].op == RE_CHAR) {
        buf[n] = (char)c->code[pc + n].a;
        n++;
      }
      int clen = re_utf8_charlen(buf, buf + n);
      if (clen < 1) clen = 1;
      len += clen;
      chars += 1;
      pc += (uint32_t)clen;
      break;
    }
    case RE_CLASS:
    case RE_NCLASS:
    case RE_ANY:
    case RE_ANY_NL:
      /* one character whatever its members can be, since the executor hands a
         class one decoded character at a time. The BYTE count is only right
         for a byte-indexed subject, which is why the rewind counts characters
         for every other one. */
      len += 1;
      chars += 1;
      pc++;
      break;
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
      /* Alternation: measure both branches and accept when they agree. The
         rewind is one number, so branches of different widths have no single
         answer and are refused as before; equal ones are as measurable as a
         plain sequence (`(?<=[^ab]|x)` is one character either way). Branch 1
         runs from pc+1 to the JMP that skips branch 2, whose target is where
         the two join. */
      uint32_t b2 = inst.offset;
      if (b2 <= pc + 1 || b2 > end) return -1;
      uint32_t jmp = b2 - 1;
      if (c->code[jmp].op != RE_JMP) return -1;
      uint32_t join = c->code[jmp].offset;
      /* The join may sit past this range: a branch of an OUTER alternation
         ends at a JMP to the outer join, and the walk of this range stops
         there of its own accord. */
      if (join < b2 || join > c->code_len) return -1;
      int ch1 = 0, ch2 = 0;
      int len1 = compute_fixed_len(c, pc + 1, jmp, &ch1);
      int len2 = compute_fixed_len(c, b2, join, &ch2);
      if (len1 < 0 || len2 < 0 || ch1 != ch2) return -1;
      /* Branches of the same character width may still differ in bytes
         (`[^ab]|µ`), and the rewind is by characters everywhere but a
         byte-indexed subject. Report a byte width of zero for that case: the
         executor takes it as "no byte width" and declines to rewind there,
         rather than rewinding by a width only one branch has. */
      len += (len1 == len2) ? len1 : 0;
      chars += ch1;
      pc = join;
      break;
    }
    case RE_MATCH:
      *chars_out = chars;
      return len;
    default:
      return -1;  /* unknown/variable-length instruction */
    }
  }
  *chars_out = chars;
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
  c->last_atom_comment = FALSE;

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
          c->last_atom_comment = TRUE;
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
        else if (c->p[1] == '>') {
          /* atomic group (?>...): match the sub-pattern once and keep no
             backtracking point inside it (#3636) */
          next_char(c); next_char(c);  /* skip ?> */
          uint32_t at_pos = emit(c, RE_ATOMIC, 0, 0);
          compile_alt(c);
          emit(c, RE_MATCH, 0, 0);     /* end of the committed sub-pattern */
          c->code[at_pos].offset = (uint16_t)c->code_len;
          if (peek(c) != ')') compile_error(c, "unmatched '('");
          next_char(c);
          c->needs_backtrack = TRUE;
          c->flags = saved_flags;
          break;
        }
        else if (c->p[1] == '<' && c->p + 2 < c->src_end && (c->p[2] == '=' || c->p[2] == '!')) {
          /* lookbehind (?<=...) or (?<!...) */
          mrb_bool negative = (c->p[2] == '!');
          next_char(c); next_char(c); next_char(c);  /* skip ?<= or ?<! */
          uint32_t lb_pos = emit(c, negative ? RE_NEG_LOOKBEHIND : RE_LOOKBEHIND, 0, 0);
          emit(c, RE_LB_WIDTH, 0, 0);
          uint32_t sub_start = c->code_len;
          compile_alt(c);
          emit(c, RE_MATCH, 0, 0);
          c->code[lb_pos].offset = (uint16_t)c->code_len;

          /* measure the sub-pattern in both units: bytes for a byte-indexed
             (binary) subject, characters for a UTF-8 one */
          int fixed_chars = 0;
          int fixed_len = compute_fixed_len(c, sub_start, c->code_len, &fixed_chars);
          if (fixed_len < 0) {
            compile_error(c, "lookbehind must be fixed length");
          }
          if (fixed_len > 255) {
            compile_error(c, "lookbehind too long (max 255 bytes)");
          }
          c->code[lb_pos].a = (uint8_t)fixed_len;
          /* the character count never exceeds the byte count, so it fits */
          c->code[lb_pos + 1].a = (uint8_t)fixed_chars;

          if (peek(c) != ')') compile_error(c, "unmatched '('");
          next_char(c);
          c->needs_backtrack = TRUE;  /* needs backtracking engine */
          c->flags = saved_flags;
          break;
        }
        else if (c->p[1] == '\'' ||
                 (c->p[1] == '<' && c->p + 2 < c->src_end &&
                  c->p[2] != '=' && c->p[2] != '!')) {
          /* A named capture, in either spelling: (?<name>...) or (?'name'...).
             The quoted one needs none of the ruling out the angled one does
             above, since no lookbehind is spelled with a quote. The name runs
             to its own terminator, so a '>' inside quotes and a quote inside
             angles are both members of the name. The scan that decides
             whether a plain group still captures already read both spellings,
             so the parser reading only one left `(?'x'a)` demoting the plain
             groups and then falling through to a stray `?`. */
          int close = (c->p[1] == '<') ? '>' : '\'';
          next_char(c); next_char(c);  /* skip ?< or ?' */
          cap_name = c->p;
          while (peek(c) != close && peek(c) >= 0) {
            /* CRuby's fetch_name() ends the scan at a ')' instead of taking it
               into the name, and a name no delimiter ended is `invalid group
               name`. The first byte is exempt, the test starting one byte in,
               so (?<)>x) names ")" in both engines. The message quotes to the
               end of the pattern, as CRuby does when the scan never reached a
               delimiter. */
            if (peek(c) == ')' && c->p > cap_name)
              compile_error_group_name(c, cap_name, (size_t)(c->src_end - cap_name));
            next_char(c);
          }
          if (peek(c) != close) compile_error(c, "unterminated named capture");
          if (c->p == cap_name) compile_error(c, "group name is empty");
          /* A definition names a group, it never numbers one: CRuby reads a
             leading digit or '-' as the number spelling, which only a
             reference may use, and refuses it here. Everything after the first
             byte is a name to both engines, '-' and spaces included, so this
             is not a restriction to word characters. Without it, (?<1>x)
             defined a group that no \k could reach, the reference side
             reading digits as a number. */
          if (*cap_name == '-' || (*cap_name >= '0' && *cap_name <= '9'))
            compile_error_group_name(c, cap_name, (size_t)(c->p - cap_name));
          if (!RE_NAME_LEN_FITS(c->p - cap_name)) compile_error(c, "group name too long");
          cap_name_len = (uint16_t)(c->p - cap_name);
          next_char(c);  /* skip the closing > or ' */
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

      /* a named group in the pattern turns off numbered capturing (#3678) */
      if (capturing && !cap_name && c->has_named_group) capturing = FALSE;
      uint16_t group = 0;
      if (capturing) {
        if (c->num_captures >= RE_MAX_CAPTURES) {
          /* Name the ceiling: without it the message says a pattern is too
             wide without saying what would fit, and the number is not one a
             reader can guess. */
          char buf[64];
          snprintf(buf, sizeof buf, "too many capture groups (maximum %d)",
                   RE_MAX_CAPTURES - 1);
          compile_error(c, buf);
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

      uint32_t grp_body = c->code_len;
      compile_alt(c);

      if (peek(c) != ')') compile_error(c, "unmatched '('");
      next_char(c);

      if (capturing) {
        emit(c, RE_SAVE, 0, group * 2 + 1);
        /* remember the body's code range so `\g<name>` can re-emit it */
        if (group < RE_MAX_CAPTURES) {
          c->grp_start[group] = grp_body;
          c->grp_len[group] = c->code_len - 1 - grp_body;
        }
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
      emit(c, RE_BACKREF, (uint8_t)(ch - '0'), (c->flags & RE_FLAG_IGNORECASE) ? 1 : 0);
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
    else if (ch == 'G') {
      /* \G: the position the search started from -- for a plain #match that
         is the string start, and for a scan/gsub step the point the previous
         match ended (#3637) */
      next_char(c);
      emit(c, RE_GPOS, 0, 0);
      c->needs_backtrack = TRUE;
    }
    else if (ch == 'g' && c->p + 1 < c->src_end &&
             (c->p[1] == '<' || c->p[1] == '\'')) {
      /* \g<name> / \g<n>: a subexpression CALL -- match what that group
         matches, here. A group already compiled is re-emitted as a copy, so
         its own captures update like CRuby's; a self- or forward reference
         would need real recursion and is refused rather than mismatched. */
      next_char(c);  /* skip g */
      int close = (peek(c) == '<') ? '>' : '\'';
      next_char(c);
      const char *gname = c->p;
      while (peek(c) != close && peek(c) >= 0) next_char(c);
      if (peek(c) != close) compile_error(c, "unterminated group reference");
      uint16_t gname_len = (uint16_t)(c->p - gname);
      next_char(c);
      int gnum = -1;
      if (gname_len > 0 && (gname[0] == '-' || (gname[0] >= '0' && gname[0] <= '9'))) {
        mrb_bool relative = (gname[0] == '-');
        int n = 0;
        for (uint16_t i = (relative ? 1 : 0); i < gname_len; i++) {
          if (gname[i] < '0' || gname[i] > '9') compile_error(c, "invalid group reference");
          n = n * 10 + (gname[i] - '0');
        }
        gnum = relative ? (int)c->num_captures - n : n;
      }
      else {
        for (uint16_t i = 0; i < c->num_named; i++) {
          if (c->named_captures[i].name_len == gname_len &&
              memcmp(c->named_captures[i].name, gname, gname_len) == 0) {
            gnum = c->named_captures[i].group;
            break;
          }
        }
      }
      if (gnum < 1 || gnum >= RE_MAX_CAPTURES || c->grp_len[gnum] == 0)
        compile_error(c, "undefined group reference");
      emit_atom_copy(c, c->grp_start[gnum], c->grp_len[gnum]);
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
      while (peek(c) != close && peek(c) >= 0) {
        /* The reference reads a name the same way a definition does, and stops
           at a ')' the same way too, from the second byte on: \k<)> reaches the
           group (?<)>x) opened, while \k<a)b> is `invalid group name`. */
        if (peek(c) == ')' && c->p > name)
          compile_error_group_name(c, name, (size_t)(c->src_end - name));
        next_char(c);
      }
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
      emit(c, RE_BACKREF, (uint8_t)group, (c->flags & RE_FLAG_IGNORECASE) ? 1 : 0);
      c->has_backref = TRUE;
    }
    else if (ch == 'K' || ch == 'R' || ch == 'X') {
      /* Each of these means something in CRuby that this engine does not do:
         `\K` drops what was matched before it, `\R` is any linebreak and `\X`
         is a whole grapheme cluster. Left to the fall-through each was simply
         its own letter, so /\R/ matched an R rather than a newline. Inside a
         character class CRuby reads them as the letter too, which is what the
         class parser already does, so only the escape outside one is refused
         here. Upstream refuses `\G` and `\g<name>` alongside these; both are
         carried here already (RE_GPOS and the subexpression-call arm above),
         so they stay. */
      {
        char ebuf[64];
        snprintf(ebuf, sizeof(ebuf), "\\%c is not supported", (char)ch);
        compile_error(c, ebuf);
      }
    }
    else if ((ch == 'p' || ch == 'P') && c->p + 1 < c->src_end && c->p[1] == '{') {
      /* A property outside a class is a one-member class: the same set, with
         nothing else in it. Only the braced spelling is a property -- CRuby
         reads a bare `\p`, and `\pL` as well, as the letter, and so does the
         fall-through below. */
#ifdef RE_UNICODE_CTYPE
      uint16_t id = add_class(c);
      char pname[64];
      const char *pp = c->p;
      if (class_read_property(&c->classes[id], &pp, c->src_end, ch == 'P',
                              pname, sizeof(pname))) {
        re_charclass *pcc = &c->classes[id];
        c->p = pp;
        /* A POSIX-named property routed to a ctype bit needs the same /i
           closure a bracket gets, and for the same reason: the type is read at
           match time rather than spelled out as members, so nothing folded it.
           compile_charclass does this for a class written with `[...]`; a bare
           `\p{...}` is a class too, just one nothing else is in. */
        pcc->ctype_fold = (c->flags & RE_FLAG_IGNORECASE) &&
                          (pcc->ctype_yes || pcc->ctype_no);
        if (c->flags & RE_FLAG_IGNORECASE) class_fold_codepoints(c, pcc);
        emit(c, RE_CLASS, (uint8_t)id, 0);
      }
      else {
        char ebuf[128];
        snprintf(ebuf, sizeof(ebuf), "character property is not supported: %s", pname);
        compile_error(c, ebuf);
      }
#else
      compile_error(c, "character property is not supported");
#endif
    }
    else if (ch == 'u') {
      next_char(c);  /* skip u */
      mrb_bool more;
      uint32_t cp = unicode_escape_first(c, &more);
      uint32_t nx;
      /* A `\u{...}` list is a sequence of atoms rather than one, so a
         quantifier after it repeats the last codepoint only: /\u{61 62}+/ is
         `a` followed by `b+`. Moving atom_start past the codepoints already
         emitted is what leaves the last one as the target. */
      while (unicode_escape_next(c, &more, &nx)) {
        emit_codepoint(c, cp);
        c->atom_start = c->code_len;
        cp = nx;
      }
      emit_codepoint(c, cp);
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
    if (ch >= 128) {
      /* Under /i the character is emitted as the class of its counterparts
         instead: a counterpart need not have the same width, so a run of bytes
         could not express it. */
      {
        int dlen = 0;
        uint32_t dcp = re_utf8_decode(c->p - 1, c->src_end, &dlen);
        if (dlen > 1 && emit_cp_folded(c, dcp)) {
          c->p += dlen - 1;
          break;
        }
      }
      /* Emit every byte of a multibyte character here, so the whole character
         is one atom. Leaving the continuation bytes to the parse loop made
         each of them an atom of its own, and a quantifier binds to the last
         atom emitted: /Ā+/ compiled as \xC4(\x80)+ and matched one Ā in "ĀĀ".
         An invalid lead byte has a charlen of 1 and still emits alone.
         (ported from mruby-regexp bcacba1e1) */
      int len = re_utf8_charlen(c->p - 1, c->src_end);
      emit(c, RE_CHAR, (uint8_t)ch, 0);
      for (int i = 1; i < len; i++) {
        int b = next_char(c);
        if (b < 0) break;
        emit(c, RE_CHAR, (uint8_t)b, 0);
      }
      break;
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
    /* the lookarounds and the atomic group carry an end-of-sub-pattern
       target, which relocates exactly like a jump */
    case RE_LOOKAHEAD: case RE_NEG_LOOKAHEAD:
    case RE_LOOKBEHIND: case RE_NEG_LOOKBEHIND: case RE_ATOMIC:
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

/* Wrap the code from `start` to the end in an atomic group: the possessive
   quantifiers (a++, a*+, a?+, a{n,m}+) are exactly `(?>a+)` and friends. */
static void
wrap_atomic(re_compiler *c, uint32_t start)
{
  insert_inst(c, start, RE_ATOMIC, 0, 0);
  emit(c, RE_MATCH, 0, 0);
  c->code[start].offset = (uint16_t)c->code_len;
  c->needs_backtrack = TRUE;
}

/* Emit a non-ASCII codepoint under /i as the class of its case counterparts
   rather than as a run of bytes, and report whether it did. A counterpart need
   not have the same byte length -- U+212A folds to `k` -- and RE_CLASS decodes
   one codepoint and compares that, so the widths need not agree. FALSE when the
   codepoint has no counterpart, or when this build carries no folding for it.
   (ported from mruby-regexp 618ba9435) */
static mrb_bool
emit_cp_folded(re_compiler *c, uint32_t cp)
{
  if (cp < 128 || !(c->flags & RE_FLAG_IGNORECASE)) return FALSE;
  uint32_t alts[RE_CASE_ALTS_MAX];
  int n = re_case_alts(cp, alts);
  if (n <= 1 && re_case_fold(cp) == cp) return FALSE;
  uint16_t id = add_class(c);
  for (int i = 0; i < n; i++) {
    if (alts[i] < 128) class_set_bit(&c->classes[id], (uint8_t)alts[i]);
    else class_add_codepoint(&c->classes[id], alts[i]);
  }
  if (cp >= 128) class_add_codepoint(&c->classes[id], cp);
  emit(c, RE_CLASS, (uint8_t)id, 0);
  return TRUE;
}

/* Emit one codepoint as an atom: a run of RE_CHAR, one per UTF-8 byte. This is
   the literal path for a codepoint the pattern NAMES rather than spells, so the
   bytes come from the encoder instead of from the pattern. The run has to be a
   single atom just the same, or a following quantifier binds to the last byte
   alone. (ported from mruby-regexp 048e5da5f) */
static void
emit_codepoint(re_compiler *c, uint32_t cp)
{
  if (cp < 128) {
    if ((c->flags & RE_FLAG_IGNORECASE) &&
        ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))) {
      uint16_t id = add_class(c);
      class_set_bit(&c->classes[id], (uint8_t)cp);
      class_set_bit(&c->classes[id], (uint8_t)(cp >= 'a' ? cp - 32 : cp + 32));
      emit(c, RE_CLASS, (uint8_t)id, 0);
      return;
    }
    emit(c, RE_CHAR, (uint8_t)cp, 0);
    return;
  }
  if (emit_cp_folded(c, cp)) return;
  char buf[4];
  int len = re_utf8_encode(cp, buf);
  for (int i = 0; i < len; i++) emit(c, RE_CHAR, (uint8_t)buf[i], 0);
}

/* Compile atom with quantifiers (*, +, ?, {n,m}) */
static void
compile_quantified(re_compiler *c)
{
  uint32_t begin = c->code_len;
  const char *atom_start = c->p;
  /* atom_start normally stays at `begin`; compile_atom moves it only for a
     `\u{...}` list, whose leading codepoints are atoms of their own. Saving and
     restoring it keeps a nested compile_quantified (inside a group) from
     leaving its own atom behind for this one. */
  uint32_t saved_atom_start = c->atom_start;
  c->atom_start = begin;
  compile_atom(c);
  uint32_t start = c->atom_start;
  c->atom_start = saved_atom_start;
  if (c->code_len == begin) {
    /* Issue #825: when compile_atom emitted nothing AND the next
       char is a bare quantifier (star, plus, question), the
       surrounding seq loop has nothing to advance with and spins
       forever. Raise instead. CRuby: RegexpError "target of
       repeat operator is not specified". */
    int qch = peek(c);
    if (qch == '*' || qch == '+' || qch == '?') {
      /* Unless an atom WAS read and simply had nothing to emit -- an empty
         group. Repeating an empty match answers the same empty match
         however many times it runs, so the quantifier is consumed and nothing
         is emitted: a quantified empty group matches "" as it does in CRuby,
         where the raise is reserved for a quantifier with no atom at all. */
      if (c->p == atom_start || c->last_atom_comment) {
        compile_error(c, "target of repeat operator is not specified");
      }
      next_char(c);                                   /* the quantifier */
      if (peek(c) == '?' || peek(c) == '+') next_char(c);  /* lazy / possessive */
    }
    else if (qch == '{' && c->p != atom_start && !c->last_atom_comment) {
      /* the counted form over the same empty atom, with a literal '{' left
         to the seq loop when it spells no quantifier */
      const char *brace = c->p;
      next_char(c);
      int qmin, qmax;
      if (parse_quantifier(c, &qmin, &qmax)) {
        if (peek(c) == '?' || peek(c) == '+') next_char(c);
      }
      else c->p = brace;
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
    /* possessive (a++ / a*+ / a?+): the quantifier keeps no backtracking
       point, i.e. the whole repetition is an atomic group (#3636) */
    mrb_bool possessive = FALSE;
    if (!nongreedy && peek(c) == '+') { next_char(c); possessive = TRUE; }


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
    if (possessive) wrap_atomic(c, start);
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
      /* The bitmap holds the 128 ASCII bytes, and the matcher never skips a
         byte >= 128 (FIRST_BYTE_OK in re_exec.c), so a non-ASCII leading
         byte -- the first byte of a multibyte literal, emitted one RE_CHAR
         per UTF-8 byte -- is accepted without a bit. Indexing the bitmap with
         it wrote past bm[16] on re_compile's stack (0xE9 >> 3 = 29): a
         pattern beginning with an accented letter corrupted the frame. */
      if (code[pc].a < 128) bm[code[pc].a >> 3] |= (1 << (code[pc].a & 7));
      return TRUE;
    case RE_CLASS: {
      const re_charclass *cc = &classes[code[pc].a];
      for (int i = 0; i < 16; i++) bm[i] |= cc->bitmap[i];
      if (!class_is_ascii_only(cc)) return FALSE;  /* non-ASCII possible */
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

/* Is there a path from `pc` to `goal` that consumes nothing? The walk follows
   the epsilon opcodes only, so a body that must consume input answers FALSE.
   `seen` is stamped with `mark` to keep the walk finite. (ported from
   mruby-regexp 45c588a83) */
static mrb_bool
epsilon_path(const re_inst *code, uint32_t pc, uint32_t goal,
             uint32_t *seen, uint32_t mark)
{
  while (pc != goal) {
    if (pc > goal || seen[pc] == mark) return FALSE;
    seen[pc] = mark;
    switch (code[pc].op) {
    case RE_SAVE:
    case RE_BOL: case RE_EOL: case RE_BOT: case RE_EOT: case RE_EOTNL:
    case RE_WBOUND: case RE_NWBOUND:
    /* A backreference to a group that captured empty consumes nothing, so it
       can lie on a path that need not consume. */
    case RE_BACKREF:
      pc++;
      break;
    case RE_JMP:
    /* A lookaround is zero-width whatever its sub-pattern does, so the walk
       steps over the sub-pattern to the end the instruction records.
       (ported from mruby-regexp 2ea26d60f) */
    case RE_LOOKAHEAD: case RE_NEG_LOOKAHEAD:
    case RE_LOOKBEHIND: case RE_NEG_LOOKBEHIND:
      pc = code[pc].offset;
      break;
    /* An atomic group is zero-width exactly when its sub-pattern can match
       empty -- unlike a lookaround it consumes what that sub-pattern does.
       The sub-pattern runs from pc+1 and ends at its own MATCH, which sits
       just before the instruction `offset` names, so that is the goal the
       inner walk is given. */
    case RE_ATOMIC:
      if (!epsilon_path(code, pc + 1, code[pc].offset - 1, seen, mark)) return FALSE;
      pc = code[pc].offset;
      break;
    case RE_SPLIT:
    case RE_SPLITNG:
      if (epsilon_path(code, code[pc].offset, goal, seen, mark)) return TRUE;
      pc++;
      break;
    default:
      return FALSE;  /* consumes input, or is an assertion this walk cannot judge */
    }
  }
  return TRUE;
}

/* Flag every backward edge that closes a repetition whose body can run empty,
   in `a` on the edge opcode (which nothing else uses), and answer how deeply
   such repetitions nest -- the number of passes one VM step may need. */
static uint8_t
mark_empty_loops(re_inst *code, uint32_t code_len)
{
  uint32_t n = code_len + 1;
  int32_t *delta = (int32_t*)calloc(2 * (size_t)n, sizeof(int32_t));
  if (!delta) return 0;
  uint32_t *seen = (uint32_t*)(delta + n);
  uint32_t mark = 0;

  for (uint32_t pc = 0; pc < code_len; pc++) {
    re_inst in = code[pc];
    if (in.op != RE_JMP && in.op != RE_SPLIT && in.op != RE_SPLITNG) continue;
    code[pc].a = 0;                /* this pass owns `a` on the edge opcodes */
    if (in.offset > pc) continue;  /* forward edge: alternation, not a loop */
    if (!epsilon_path(code, in.offset, pc, seen, ++mark)) continue;
    code[pc].a = 1;
    delta[in.offset]++;
    delta[pc + 1]--;  /* the closing edge itself still sits inside the loop */
  }

  int32_t depth = 0, max = 0;
  for (uint32_t pc = 0; pc < code_len; pc++) {
    depth += delta[pc];
    if (depth > max) max = depth;
  }
  free(delta);
  return max > UINT8_MAX ? UINT8_MAX : (uint8_t)max;
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
  c.orig_flags = flags;
  c.num_captures = 1;  /* group 0 = whole match */
  c.has_named_group = re_src_has_named_group(c.p, c.src_end);

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

  /* Which backward edges close a repetition whose body can run empty, and how
     deeply such repetitions nest: the VM needs both (see add_thread). */
  pat->loop_depth = mark_empty_loops(pat->code, pat->code_len);

  /* Pre-allocate VM state cache for pike_vm */
  {
    int list_capa = RE_LIST_CAPA(pat->code_len, pat->loop_depth);
    pat->cached_visited = (uint32_t*)calloc(pat->code_len + 1, sizeof(uint32_t));
    pat->cached_threads[0] = malloc(sizeof(re_thread_cache) * list_capa);
    pat->cached_threads[1] = malloc(sizeof(re_thread_cache) * list_capa);
    pat->cached_list_capa = list_capa;
    pat->cache_in_use = FALSE;
  }

  if (c.stripped) free(c.stripped);
  pat->source = (char *)malloc((size_t)orig_len + 1);
  if (pat->source) { memcpy(pat->source, orig_pattern, (size_t)orig_len); pat->source[orig_len] = 0; }
  pat->source_len = (uint32_t)orig_len;
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
#ifdef RE_UNICODE_CTYPE
        free(pat->classes[i].props);
#endif
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
#ifndef SP_RT_COLD   /* sp_types.h; this file does not include it */
#define SP_RT_COLD
#endif
const char *re_group_name(const mrb_regexp_pattern *pat, int group) {
  SP_RT_COLD static char buf[4][64];
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
/* ...and its byte length, which a caller handing the text to the Ruby side
   needs: `pat->source` is a plain malloc'd C string, so a NUL inside it would
   end the value at a strlen. */
uint32_t sp_re_source_len(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  return (pat && pat->source) ? pat->source_len : 0;
}
/* #inspect and #to_s render the source between delimiters, so a literal `/`
   that is not already backslash-escaped must be escaped (#3061). Returns a
   freshly malloc'd buffer the caller frees. */
/* Regexp#options: CRuby's public option bits IGNORECASE=1, EXTENDED=2,
   MULTILINE=4 (the /m "dot matches newline", our internal DOTALL). The
   internal RE_FLAG_MULTILINE (^/$ at line ends) is not a Ruby-visible option. */
/* returns the runtime's sp_int (intptr_t), as sp_re.h declares it: mrb_int
   is int64_t, which is the same width only on LP64 */
intptr_t sp_re_options(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  uint32_t f = pat ? pat->flags : 0;
  intptr_t o = 0;
  if (f & RE_FLAG_IGNORECASE) o |= 1;
  if (f & RE_FLAG_EXTENDED)   o |= 2;
  if (f & RE_FLAG_DOTALL)     o |= 4;
  return o;
}
/* Regexp#== / #eql? is source AND options: /ab/ and /ab/i are different
   patterns, and comparing only the source made them equal (#3631). */
/* declared sp_bool (a C bool) by the runtime: returned as one, not as the
   int mrb_bool, which only reads the same in a 32-bit-wide return register */
bool sp_re_eq(void *a, void *b) {
  if (a == b) return 1;
  if (!a || !b) return 0;
  const char *sa = sp_re_source(a), *sb = sp_re_source(b);
  if (!sa || !sb) return sa == sb;
  return strcmp(sa, sb) == 0 && sp_re_options(a) == sp_re_options(b);
}
bool sp_re_casefold_p(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  return (pat && (pat->flags & RE_FLAG_IGNORECASE)) ? true : false;
}
/* The engine's own internal flag word, for re-compiling a copy (Regexp.new(re))
   with the source pattern's exact options preserved. */
uint32_t sp_re_raw_flags(void *vpat) {
  mrb_regexp_pattern *pat = (mrb_regexp_pattern *)vpat;
  return pat ? pat->flags : 0;
}
/* Translate the public Regexp option bits (IGNORECASE=1, EXTENDED=2,
   MULTILINE=4) that Regexp.new's second argument carries into the internal
   flag bits re_compile expects (#3055). */
uint32_t sp_re_opts_to_flags(intptr_t o) {   /* the runtime's sp_int, as sp_re.h declares it */
  uint32_t f = 0;
  if (o & 1) f |= RE_FLAG_IGNORECASE;
  if (o & 2) f |= RE_FLAG_EXTENDED;
  if (o & 4) f |= RE_FLAG_DOTALL;   /* Ruby MULTILINE == "dot matches newline" */
  return f;
}

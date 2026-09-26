/*
** re_internal.h - standalone regexp engine for Spinel
**
** Derived from mruby-regexp. No mruby runtime dependency.
*/

#ifndef SP_RE_INTERNAL_H
#define SP_RE_INTERNAL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Standalone type definitions (replacing mruby types) */
typedef int64_t mrb_int;
typedef int mrb_bool;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* The two Unicode tables this engine carries, each left out by asking for it:
   -DRE_NO_UNICODE_CASE drops the case foldings /i reads, -DRE_NO_UNICODE_CTYPE
   the character types a POSIX bracket and `\b` read. The names sit here rather
   than beside the declarations because re_charclass, below, carries a field
   only the second build has. What each table is and what dropping it costs is
   written where the functions reading it are declared. */
#ifndef RE_NO_UNICODE_CASE
# define RE_UNICODE_CASE
#endif
#ifndef RE_NO_UNICODE_CTYPE
# define RE_UNICODE_CTYPE
#endif

/* Bytecode instructions for the NFA engine */
enum re_opcode {
  RE_CHAR,       /* match literal byte: operand = byte value */
  RE_ANY,        /* match any character (. without DOTALL) */
  RE_ANY_NL,     /* match any character including newline (. with DOTALL) */
  RE_CLASS,      /* match character class: operand = class_id */
  RE_NCLASS,     /* match negated character class: operand = class_id */
  RE_MATCH,      /* successful match */
  RE_JMP,        /* unconditional jump: operand = target offset */
  RE_SPLIT,      /* fork: operand = target offset (greedy: try next first) */
  RE_SPLITNG,    /* fork: operand = target offset (non-greedy: try jump first) */
  RE_SAVE,       /* save capture position: operand = slot number */
  RE_BOL,        /* assert beginning of line (^) */
  RE_EOL,        /* assert end of line ($) */
  RE_BOT,        /* assert beginning of text (\A) */
  RE_EOT,        /* assert end of text (\z) */
  RE_EOTNL,     /* assert end of text or before final \n (\Z) */
  RE_WBOUND,     /* assert word boundary (\b) */
  RE_NWBOUND,    /* assert non-word boundary (\B) */
  RE_BACKREF,    /* backreference: operand = group number */
  RE_LOOKAHEAD,  /* positive lookahead: offset = end of sub-pattern */
  RE_NEG_LOOKAHEAD, /* negative lookahead: offset = end of sub-pattern */
  RE_LOOKBEHIND,     /* positive lookbehind: a = byte length, offset = end */
  RE_NEG_LOOKBEHIND, /* negative lookbehind: a = byte length, offset = end */
  RE_LB_WIDTH,   /* payload slot following a lookbehind: a = the sub-pattern's
                    length in CHARACTERS, which is the unit the executor
                    rewinds by (ported from mruby-regexp 103e1a8bc) */
  RE_GPOS,       /* assert the search-start position (\G) */
  RE_ATOMIC,     /* atomic group (?>...) / possessive quantifier: match the
                    sub-pattern once and commit, offset = end of sub-pattern */
};

/* Bytecode instruction (4 bytes each for alignment) */
typedef struct {
  uint8_t op;
  uint8_t a;       /* small operand or class id */
  uint16_t offset;  /* jump target or extended operand */
} re_inst;

/* Character class bitmap (ASCII range) */
#define RE_CLASS_BITMAP_SIZE 16  /* 128 bits = 16 bytes for ASCII */
typedef struct {
  uint8_t bitmap[RE_CLASS_BITMAP_SIZE];  /* bitmap for 0-127 */
  /* Non-ASCII codepoint ranges. Stored as flat (lo, hi) pairs:
     ranges[2k] = lo, ranges[2k+1] = hi (inclusive). NULL when the
     class has no non-ASCII members (the common case). */
  uint32_t *ranges;
  uint32_t num_ranges;   /* uint32_t: doubling a uint16_t capa from 32768
                            wrapped to 0 and fed a size-0 realloc (mruby
                            #6937), so a huge class wrote through NULL */
  uint32_t range_capa;
  mrb_bool negated;
  mrb_bool utf8_any;  /* match any non-ASCII byte if true */
#ifdef RE_UNICODE_CTYPE
  /* What the POSIX brackets in the class hold above ASCII, as re_ctype bits:
     a character belongs when its type has a bit of ctype_yes, or lacks a bit
     of ctype_no ([:^alpha:] is every character that is not a letter). Neither
     is spelled out as ranges: a class holding [[:alpha:]] would carry the
     letters as hundreds of ranges, and be read through them one by one at
     every character.

     ctype_fold is set under /i when either is: the type read is then that of
     the character and of every character sharing its folding, so that
     [[:upper:]] under /i holds "ā" through "Ā". A member the class holds by
     bit or by range is closed under folding at compile time instead; see
     compile_charclass(). */
  uint16_t ctype_yes;
  uint16_t ctype_no;
  mrb_bool ctype_fold;
  /* The `\p{...}` properties named in the class, each with whether it was
     written `\P{...}`. A list rather than a mask because there are more
     properties than a mask would hold, and a class names one or two of them;
     the ctype bits stay a mask because there are eleven of those and a
     bracket class often names several. */
  struct { int16_t id; mrb_bool negated; } *props;
  int num_props;
  int prop_capa;
#endif
} re_charclass;

/* The stored name length is bounded by the field that holds it: a longer name
   is refused rather than silently truncated into a different name (ported from
   mruby-regexp, which widened the field instead). */
#define RE_MAX_NAME_LEN UINT16_MAX
#define RE_NAME_LEN_FITS(n) ((uintmax_t)(n) <= RE_MAX_NAME_LEN)

/* A pike_vm step walks a repetition's body once per nesting level, so that a
   loop's final empty iteration can finish even when the closure resumed inside
   the body and already marked that iteration's tail (see add_thread). The cap
   keeps a pathologically nested pattern from growing the thread lists with the
   square of the program; past it, such a pattern keeps the older, stale-capture
   behaviour rather than costing memory. (ported from mruby-regexp 45c588a83) */
#define RE_MAX_PASS 4
#define RE_PASS_SPAN(depth) \
  ((uint32_t)((depth) < RE_MAX_PASS ? (depth) : RE_MAX_PASS) + 1)

/* Capacity of one pike_vm thread list, shared by the VM and by the cache the
   compiler pre-allocates for it so the two cannot drift. An instruction
   enqueues at most one thread per pass, and threads waiting on a later sp are
   carried over from the previous step on top of that. */
#define RE_LIST_CAPA(code_len, depth) \
  ((int)(code_len) * (int)(RE_PASS_SPAN(depth) + 1) + 16)

/* Named capture entry */
typedef struct {
  const char *name;
  uint16_t name_len;
  uint16_t group;
} re_named_capture;

/* Compiled regexp pattern */
typedef struct mrb_regexp_pattern {
  re_inst *code;          /* bytecode array */
  uint32_t code_len;      /* number of instructions */
  re_charclass *classes;   /* character class table */
  uint16_t num_classes;
  uint16_t num_captures;   /* number of capture groups (including group 0) */
  uint32_t flags;
  re_named_capture *named_captures;
  uint16_t num_named;
  mrb_bool has_backref;    /* true if pattern uses \1-\9 */
  mrb_bool needs_backtrack; /* true if pattern needs backtracking engine */
  uint8_t *prefix;         /* literal prefix bytes for fast skip (or NULL) */
  uint8_t prefix_len;      /* length of prefix (0 = no prefix) */
  uint8_t first_bytes[16]; /* bitmap of possible first bytes (128-bit, ASCII) */
  mrb_bool has_first_bytes; /* true if first_bytes is usable for skipping */
  mrb_bool is_literal;     /* true if pattern is pure literal (no metacharacters) */
  /* Cached VM state for pike_vm (avoids malloc per re_exec call) */
  uint8_t loop_depth;           /* deepest nesting of repetitions whose body
                                   can match empty (see RE_MAX_PASS) */
  uint32_t *cached_visited;     /* generation-based visited array */
  void *cached_threads[2];      /* curr/next thread lists */
  int cached_list_capa;         /* capacity of cached thread lists */
  mrb_bool cache_in_use;        /* re-entrancy guard */
  char *source;                 /* the pattern text, for #source/#inspect/#to_s */
  uint32_t source_len;          /* its BYTE length: the text may hold a NUL,
                                   which a strlen on `source` would stop at */
} mrb_regexp_pattern;

/* Regexp flags */
#define RE_FLAG_IGNORECASE  1
#define RE_FLAG_MULTILINE   2
#define RE_FLAG_DOTALL      4
#define RE_FLAG_EXTENDED    8

/* Step limit for ReDoS protection */
#ifndef MRB_REGEXP_STEP_LIMIT
#define MRB_REGEXP_STEP_LIMIT 1000000
#endif

/* How much backtracking state one search may hold: choice points and undo
   records counted together, since each stands for a branch or a write the
   search can still take back. A greedy repetition forks once per iteration
   whatever its body holds, so what a search holds grows with the LENGTH OF
   THE SUBJECT and not with the nesting of the pattern -- and this is what
   refuses a pattern that is nothing out of the ordinary once the subject is
   long enough, so it is not a knob turned up idly. It sits on the heap, so
   it is not the C stack it protects (see MRB_REGEXP_FRAME_LIMIT).
   (ported from mruby-regexp 7c6059908) */
#ifndef MRB_REGEXP_STACK_LIMIT
#define MRB_REGEXP_STACK_LIMIT 32768
#endif

/* C frames bt_match may nest. A lookaround and an atomic group still recurse
   one frame each, but a frame is entered and left per construct rather than
   held across the text after it, so what this counts is how deeply the two
   NEST IN THE PATTERN -- `(?>(?>(?>a)))` and not `(?>a)*` over a long run,
   which spends one frame at a time whatever the run. It stays at the number
   the old ceiling had, so no pattern that used to compile its way through
   loses it; the frames are far cheaper than the old ones, since the fork per
   iteration is no longer one. Issue #777. */
#ifndef MRB_REGEXP_FRAME_LIMIT
#define MRB_REGEXP_FRAME_LIMIT 10000
#endif

/* Maximum captures, group 0 included, so 31 groups of one's own.

   The number is what the match registers hold: $~ is built from
   sp_re_caps[], the frame that saves those across a method call carries a
   copy, and both are sized by this. A pattern past it used to compile and
   then be TRUNCATED there, so `m.size` answered 32 where CRuby answered 41
   and every group above the thirty-first read nil -- the two ceilings, this
   one and the registers', disagreed and the wider one lost quietly.

   They agree now, and a pattern past it is refused rather than cut. 32 is
   where the registers sit rather than where a program is likely to need to
   stop: of the 8,135 regexp literals in CRuby 4.0.4's stdlib and bundled
   gems the widest has 8 groups, and none has more than 20. Widening it is
   not free either -- the frame is on the stack of every method that matches,
   so taking this to 128 takes a matching method's recursion depth from
   15,000 to 6,000. */
#define RE_MAX_CAPTURES 32

/* Thread struct for Pike VM. `sp` is the input position the thread is
   waiting for; the outer loop only dispatches a thread when its sp
   matches the loop's current sp, otherwise the thread is deferred to
   the next iteration. This keeps multi-byte consumers (RE_CLASS over
   a UTF-8 char, advancing 3 bytes) in sync with single-byte consumers
   (RE_CHAR, advancing 1 byte) without requiring a uniform char-step
   outer loop — both varieties just enqueue at their own sp+N. */
typedef struct {
  uint32_t pc;
  int cap_slot;
  const char *sp;
} re_thread_cache;

/* Compile a pattern string into bytecode */
mrb_regexp_pattern* re_compile(const char *pattern, mrb_int len, uint32_t flags);

/* Free a compiled pattern */
void re_free(mrb_regexp_pattern *pat);

/* Issue #781: install a callback that handles regex compile errors.
   The callback receives a formatted message and is expected NOT to
   return (typically it wraps sp_raise_cls via longjmp). Unset by
   default; the library falls back to fprintf + exit. */
void sp_re_set_error_handler(void (*fn)(const char *msg));

/* UTF-8 helpers */
int re_utf8_charlen(const char *s, const char *end);
uint32_t re_utf8_decode(const char *s, const char *end, int *len);

/* Unicode simple case folding for /i, on unless the build asks for the
   ASCII-only engine with -DRE_NO_UNICODE_CASE. The table it reads is
   lib/regexp/re_casefold.h (196 runs, generated by tools/gen_re_casefold.rb);
   without it, /i folds ASCII alone, which is what this engine did before the
   table arrived, and a non-ASCII literal under /i matches literally.
   (the option follows mruby-regexp 618ba9435 / 1ff35503d) */

/* What a POSIX bracket holds above ASCII, on the same terms: the table is
   lib/regexp/re_ctype.h (3468 runs, generated by tools/gen_re_ctype.rb), and
   -DRE_NO_UNICODE_CTYPE leaves it out. Without it a bracket holds the ASCII
   set alone, which is what this engine did before the table arrived, and
   `\b` reads that set too. (ported from mruby-regexp 55b6deab4 / 5ffcc0034) */

/* The types a POSIX bracket can name, as bits: [[:alpha:]] holds a character
   whose type has RE_CTYPE_ALPHA. Every build reads them for ASCII off the
   list in re_compile.c; above ASCII only a RE_UNICODE_CTYPE build has an
   answer, which re_ctype() reads off re_ctype.h. [:xdigit:] and [:ascii:] are
   sets ASCII defines and have no bit. */
enum re_ctype {
  RE_CTYPE_ALPHA = 1 << 0,
  RE_CTYPE_UPPER = 1 << 1,
  RE_CTYPE_LOWER = 1 << 2,
  RE_CTYPE_DIGIT = 1 << 3,
  RE_CTYPE_ALNUM = 1 << 4,
  RE_CTYPE_WORD  = 1 << 5,
  RE_CTYPE_PUNCT = 1 << 6,
  RE_CTYPE_SPACE = 1 << 7,
  RE_CTYPE_BLANK = 1 << 8,
  RE_CTYPE_GRAPH = 1 << 9,
  RE_CTYPE_PRINT = 1 << 10,
  RE_CTYPE_CNTRL = 1 << 11
};

#ifdef RE_UNICODE_CTYPE
/* The types of a codepoint above ASCII, as the bits above. */
uint16_t re_ctype(uint32_t cp);

/* Whether a class holds a codepoint above ASCII through the brackets in it
   and the utf8_any catch-all. The class matcher calls this for a class
   holding any bracket, once its ranges have said nothing. */
mrb_bool re_class_ctype_match(const re_charclass *cc, uint32_t cp);

/* Unicode properties, as `\p{...}` names them. re_prop_lookup resolves a name
   to an id (-1 when the engine does not carry it); re_prop_match answers it
   for one codepoint. The ids are three ranges rather than an enum: a general
   category, a one-letter category standing for every two-letter one under it,
   and an emoji property, which is a bit rather than an index because the emoji
   properties overlap. The POSIX names (`\p{Alpha}`, `\p{Word}`, ...) are not
   here -- the compiler routes those to the ctype bits, which is smaller and is
   what makes them fold correctly under /i. */
#define RE_PROP_GC     0
#define RE_PROP_MAJOR  256
#define RE_PROP_EMOJI  512
int      re_prop_lookup(const char *name, size_t len);
mrb_bool re_prop_match(int id, uint32_t cp);

/* Whether a class holds `cp` through the properties named in it. */
mrb_bool re_class_prop_match(const re_charclass *cc, uint32_t cp);
#endif

/* The folded form of cp: the codepoint every counterpart of it shares, or cp
   itself when it folds to nothing else. A source whose fold is several
   codepoints (U+00DF to "ss") folds to itself here -- the engine compares one
   codepoint at a time. */
uint32_t re_case_fold(uint32_t cp);

/* Every codepoint that folds the way cp does, cp included, written into out
   (at most RE_CASE_ALTS_MAX). Returns how many there are: 1 when cp has no
   counterpart, which is what tells a caller there is nothing to fold. */
#define RE_CASE_ALTS_MAX 8
int re_case_alts(uint32_t cp, uint32_t *out);
int re_utf8_encode(uint32_t cp, char *buf);
mrb_bool re_is_word_char(uint32_t c);

static inline int
re_charlen(const char *s, const char *end, mrb_bool binary)
{
  return binary ? 1 : re_utf8_charlen(s, end);
}

static inline uint32_t
re_decode_char(const char *s, const char *end, int *len, mrb_bool binary)
{
  if (binary) {
    if (len) *len = 1;
    return (uint8_t)*s;
  }
  return re_utf8_decode(s, end, len);
}

static inline mrb_bool
re_utf8_continuation_p(const char *s)
{
  return (((uint8_t)*s & 0xC0) == 0x80);
}

/* Whether a codepoint is one of the word characters a boundary sits beside.
   This is what `[[:word:]]` holds rather than what `\w` does: a boundary is
   the one thing a pattern cannot spell another way, since asking for one
   around any script takes a lookaround either side of the position, where a
   class only takes the bracket written out. So the shorthand keeps the ASCII
   set CRuby gives it, and the boundary reads every script, as CRuby's does.
   A build with no table has no answer above ASCII, and there the boundary
   reads as `[[:word:]]` does on it: the ASCII word characters and no more.
   (ported from mruby-regexp 5ffcc0034) */
static inline mrb_bool
re_word_cp(uint32_t cp)
{
  if (cp < 128) return re_is_word_char(cp);
#ifdef RE_UNICODE_CTYPE
  return (re_ctype(cp) & RE_CTYPE_WORD) != 0;
#else
  return FALSE;
#endif
}

/* Whether a codepoint a sequence `len` bytes wide spelled is a word
   character. A byte that starts no character is a byte and not the character
   it would spell inside one: re_utf8_decode hands a lone 0xB5 back as U+00B5,
   and the table would make it the word character µ. Only a codepoint some
   multi-byte sequence actually spelled is looked up, which is the test a
   binary subject would want too -- there every byte at or above 0x80 stands
   for no character. */
static inline mrb_bool
re_word_decoded(uint32_t cp, int len)
{
  return len > 1 && re_word_cp(cp);
}

/* Whether the character starting at `s` is a word character.

   A byte below 0x80 is its own character whatever the subject is, and it is
   what almost every boundary in almost every subject sits beside, so it is
   answered without decoding. A binary subject holds bytes rather than
   characters, so none of them is one. */
static inline mrb_bool
re_word_at(const char *s, const char *end, mrb_bool binary)
{
  uint8_t b = (uint8_t)*s;
  if (b < 0x80) return re_is_word_char(b);
  if (binary) return FALSE;
  int len;
  uint32_t cp = re_utf8_decode(s, end, &len);
  return re_word_decoded(cp, len);
}

/* Whether the character ending at `s` is one. Reading it takes the head of
   the character the byte before belongs to, which is the one step the engine
   ever takes backward. A head that does not decode back to `s` spelled no
   character over those bytes, so the walk is checked rather than trusted. */
static inline mrb_bool
re_word_before(const char *str, const char *s, const char *end, mrb_bool binary)
{
  uint8_t b = (uint8_t)s[-1];
  if (b < 0x80) return re_is_word_char(b);
  if (binary) return FALSE;
  const char *head = s - 1;
  while (head > str && re_utf8_continuation_p(head)) head--;
  int len;
  uint32_t cp = re_utf8_decode(head, end, &len);
  if (head + len != s) return FALSE;
  return re_word_decoded(cp, len);
}

/* Execute a match.
   Returns number of captures filled (0 = no match).
   captures[2*n] = start, captures[2*n+1] = end for group n.
   `binary` selects a byte-indexed (ASCII-8BIT) subject; 0 = UTF-8. */
int re_exec(const mrb_regexp_pattern *pat,
            const char *str, mrb_int len, mrb_int start,
            int *captures, int captures_size, mrb_bool binary);

#endif /* SP_RE_INTERNAL_H */

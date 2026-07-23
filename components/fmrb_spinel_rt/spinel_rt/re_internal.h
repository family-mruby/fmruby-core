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
} re_charclass;

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
  uint32_t *cached_visited;     /* generation-based visited array */
  void *cached_threads[2];      /* curr/next thread lists */
  int cached_list_capa;         /* capacity of cached thread lists */
  mrb_bool cache_in_use;        /* re-entrancy guard */
  char *source;                 /* the pattern text, for #source/#inspect/#to_s */
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

/* Recursion depth ceiling for bt_match. Backtracking SPLIT/SAVE
   recursion can otherwise drive the C stack to overflow on patterns
   like long alternation chains or backref + many quantifier
   iterations. 10000 frames covers realistic workloads while staying
   well under a default 8 MB stack (~40 bytes per frame). Issue #777. */
#ifndef MRB_REGEXP_DEPTH_LIMIT
#define MRB_REGEXP_DEPTH_LIMIT 10000
#endif

/* Maximum captures. Sized for realistic code: complex parsers rarely
   exceed ~50-100 capture groups. Pathological inputs like depth-500
   `((((...((a))...))))` raise RegexpError, which is the same
   "fail-gracefully on absurd input" stance as MRB_REGEXP_DEPTH_LIMIT
   above. Runtime memory scales with actual ncap per regex (see
   re_exec.c comment) so this is purely a compile-time cap. */
#define RE_MAX_CAPTURES 128

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

/* Execute a match.
   Returns number of captures filled (0 = no match).
   captures[2*n] = start, captures[2*n+1] = end for group n.
   `binary` selects a byte-indexed (ASCII-8BIT) subject; 0 = UTF-8. */
int re_exec(const mrb_regexp_pattern *pat,
            const char *str, mrb_int len, mrb_int start,
            int *captures, int captures_size, mrb_bool binary);

#endif /* SP_RE_INTERNAL_H */

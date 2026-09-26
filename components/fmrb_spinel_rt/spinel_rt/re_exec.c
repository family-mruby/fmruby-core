/*
** re_exec.c - NFA execution engine (Pike VM)
**
** Executes compiled regexp bytecode using Thompson/Pike NFA simulation.
** O(pattern * text) time complexity guarantees ReDoS resistance.
**
** See Copyright Notice in mruby.h
*/

#include "re_internal.h"
#include <string.h>

/*
 * Skip to the next position where the pattern's literal prefix could match.
 * Uses memchr on the first byte for fast scanning, then verifies the rest.
 * Returns the found position, or NULL if no match is possible.
 */
static const char*
skip_to_prefix(const mrb_regexp_pattern *pat, const char *sp, const char *str_end)
{
  if (pat->prefix_len == 0) return sp;

  uint8_t first = pat->prefix[0];
  int plen = pat->prefix_len;

  while (sp + plen <= str_end) {
    const char *found = (const char*)memchr(sp, first, str_end - sp);
    if (!found || found + plen > str_end) return NULL;
    if (plen == 1 || memcmp(found + 1, pat->prefix + 1, plen - 1) == 0) {
      return found;
    }
    sp = found + 1;
  }
  return NULL;
}

/* Check if a byte is in the first-byte bitmap */
#define FIRST_BYTE_OK(pat, ch) \
  ((ch) >= 128 || ((pat)->first_bytes[(ch) >> 3] & (1 << ((ch) & 7))))

/* Check if a codepoint matches a character class. ASCII (cp < 128) hits
   the bitmap; non-ASCII falls back to the inclusive (lo, hi) range list,
   then to the POSIX brackets the class holds, then to the utf8_any catch-all
   (used by negated shorthand like \D). */
static mrb_bool
class_match(const re_charclass *cc, uint32_t cp, mrb_bool binary)
{
  if (cp < 128) {
    return (cc->bitmap[cp >> 3] >> (cp & 7)) & 1;
  }
  for (uint32_t i = 0; i < cc->num_ranges; i++) {
    if (cp >= cc->ranges[2*i] && cp <= cc->ranges[2*i + 1]) return TRUE;
  }
#ifdef RE_UNICODE_CTYPE
  /* A binary subject holds bytes rather than characters, and a byte at or
     above 0x80 stands for no character: it has no type, so it is in the class
     through a negated bracket and not through a positive one. Asking the
     table would make the lone byte 0xB5 the word character it spells in
     UTF-8. */
  if (cc->num_props > 0) {
    /* A binary subject holds bytes, and a byte stands for no character, so it
       has no property either -- same reasoning as the brackets below. */
    if (!binary && re_class_prop_match(cc, cp)) return TRUE;
  }
  if (cc->ctype_yes || cc->ctype_no) {
    if (binary) return cc->ctype_no != 0 || cc->utf8_any;
    return re_class_ctype_match(cc, cp);
  }
#else
  (void)binary;
#endif
  return cc->utf8_any;
}

/*
 * Pike VM with optimized thread storage.
 *
 * Key optimizations vs naive approach:
 * - Captures stored in a flat pool, sized to actual ncap (not RE_MAX_CAPTURES)
 * - Generation counter for visited[] eliminates per-step memset
 * - Threads reference captures by pool index, avoiding 260-byte struct copies
 */

typedef re_thread_cache re_thread;

typedef struct {
  re_thread *threads;
  int count;
  int capa;
} re_threadlist;

/* All Pike VM state */
typedef struct {
  const mrb_regexp_pattern *pat;
  int ncap;               /* actual capture count (num_captures * 2) */
  int *cap_pool;          /* flat: cap_pool[slot * ncap .. (slot+1) * ncap) */
  int cap_pool_fixed;     /* the pool is the caller's stack block: grow by copying, never realloc */
  int pool_next;          /* next free slot */
  int pool_capa;          /* total slots allocated */
  uint32_t *visited;      /* generation-based */
  uint32_t gen;           /* visited key this step's first epsilon pass uses */
  uint32_t key_max;       /* highest key a further pass may reach this step */
  uint32_t pass_span;     /* keys one step reserves: key_max - gen + 1 */
  const char *str;
  const char *str_end;
  mrb_bool matched;
  mrb_bool match_only;    /* true: skip capture tracking (match? path) */
  mrb_bool binary;        /* true: subject is byte-indexed ASCII-8BIT */
  mrb_bool cut;           /* a higher-priority thread matched this step:
                             stop adding/processing lower-priority threads */
  mrb_bool oom;           /* the capture pool could not grow: the search holds
                             no answer about the text, so it stops rather than
                             reading a slot it was not given */
  int *result_caps;       /* best match (ncap ints) */
} pike_state;

/* Answer whether a slot was handed out, writing it through `out`, so that no
   caller can use what it was not given: the old form answered an index and a
   failed grow left the pool NULL, which the next CAP() read through. The
   refusal is recorded on the state as well, since the epsilon closure that
   asks for most of the slots is several frames deep and hands nothing back;
   add_thread() reads it where it already reads s->cut.
   (ported from mruby-regexp 73ace4d8b) */
static mrb_bool
pool_alloc(pike_state *s, int *out)
{
  if (s->pool_next >= s->pool_capa) {
    int new_capa = s->pool_capa * 2;
    int *grown;
    if (s->cap_pool_fixed) {   /* the stack block: the grown pool starts on the heap */
      grown = (int*)malloc(sizeof(int) * new_capa * s->ncap);
      if (grown) memcpy(grown, s->cap_pool, sizeof(int) * s->pool_capa * s->ncap);
      s->cap_pool_fixed = 0;
    }
    else grown = (int*)realloc(s->cap_pool, sizeof(int) * new_capa * s->ncap);
    if (!grown) { s->oom = TRUE; return FALSE; }
    s->cap_pool = grown;
    s->pool_capa = new_capa;
  }
  *out = s->pool_next++;
  return TRUE;
}

static mrb_bool
pool_copy(pike_state *s, int src_slot, int *out)
{
  int dst;
  if (!pool_alloc(s, &dst)) return FALSE;
  memcpy(&s->cap_pool[dst * s->ncap],
         &s->cap_pool[src_slot * s->ncap],
         sizeof(int) * s->ncap);
  *out = dst;
  return TRUE;
}

#define CAP(s, slot) (&(s)->cap_pool[(slot) * (s)->ncap])

/* TRUE once this step's closure has walked the loop head at pc, which means
   the body just ran without consuming: an empty iteration. */
static mrb_bool
loop_head_seen(pike_state *s, uint32_t pc)
{
  return s->visited[pc] >= s->gen;
}

/* A fork also closes e+: the body is laid out before it, so its jump target is
   the body start and pc+1 is the loop's exit. Returns the key the jump
   target's branch walks under, or RE_LOOP_STOP when the body matched empty and
   the repetition therefore has to stop. An ordinary fork, or one closing a
   loop whose body always consumes (`a` is 0, see mark_empty_loops()), has no
   empty iteration to account for and keeps the current key. */
#define RE_LOOP_STOP UINT32_MAX

static uint32_t
re_loop_back(pike_state *s, re_inst inst, uint32_t pc, uint32_t key)
{
  if (inst.offset > pc || !inst.a) return key;
  if (loop_head_seen(s, inst.offset)) return RE_LOOP_STOP;
  return key < s->key_max ? key + 1 : key;
}

/* Add thread following epsilon transitions. `key` is s->gen for the first pass
   over this step's closure and one higher per further pass, and visited[pc]
   holds the key of the pass that last walked pc: a later pass may re-walk what
   an earlier one marked, and no key is ever reused by a later step.
   (ported from mruby-regexp 45c588a83) */
static void
add_thread(pike_state *s, re_threadlist *list,
           uint32_t pc, int cap_slot, const char *sp, uint32_t key)
{
  for (;;) {
    if (s->cut || s->oom) return;
    if (pc >= s->pat->code_len) return;
    if (s->visited[pc] >= key) return;
    s->visited[pc] = key;

    re_inst inst = s->pat->code[pc];
    switch (inst.op) {
    case RE_JMP:
      /* A backward jump closes a repetition (e*, e{n,}): it returns to the
         RE_SPLIT/RE_SPLITNG head, whose offset is the loop's exit. `a` is set
         only when that body can run empty, which is the only case with a final
         empty iteration to account for. */
      if (inst.offset <= pc && inst.a) {
        uint32_t head = inst.offset;
        if (loop_head_seen(s, head)) {
          /* The head was walked at this position, so the iteration that just
             finished consumed nothing. Onigmo stops a repetition on an empty
             iteration and keeps what that iteration captured, so leave the
             loop from here rather than dying on the head's mark: this path
             outranks the exit the head itself queued before the body ran, and
             claims the exit pc first. */
          pc = s->pat->code[head].offset;
          continue;
        }
        /* The head is unmarked, so this closure resumed inside the body and
           the iteration it just finished is a real one. Run the next iteration
           in a fresh pass, past the marks the resumed tail left. */
        if (key < s->key_max) key++;
        pc = head;
        continue;
      }
      pc = inst.offset;
      continue;

    case RE_SPLIT:
      /* Greedy fork: the fall-through (pc+1) outranks the jump target, the
         same priority order the backtracking engine uses. Explore the
         higher-priority branch first so it claims shared pcs (visited[]) and
         reaches a match before the lower one. Snapshot the captures before
         pc+1's closure can mutate the shared slot; the jump branch then runs
         on that snapshot. */
      {
        uint32_t back = re_loop_back(s, inst, pc, key);
        if (back == RE_LOOP_STOP) { pc++; continue; }
        int cp = 0;
        if (!s->match_only && !pool_copy(s, cap_slot, &cp)) return;
        add_thread(s, list, pc + 1, cap_slot, sp, key);
        if (s->cut) return;
        pc = inst.offset;
        cap_slot = cp;
        key = back;
      }
      continue;

    case RE_SPLITNG:
      /* Non-greedy fork: the jump target outranks the fall-through. */
      {
        uint32_t back = re_loop_back(s, inst, pc, key);
        if (back == RE_LOOP_STOP) { pc++; continue; }
        int cp = 0;
        if (!s->match_only && !pool_copy(s, cap_slot, &cp)) return;
        add_thread(s, list, inst.offset, cap_slot, sp, back);
        if (s->cut) return;
        pc = pc + 1;
        cap_slot = cp;
      }
      continue;

    case RE_SAVE:
      /* Issue #779: bounds-check the slot offset against ncap so a
         SAVE instruction emitted for a non-existent capture group
         doesn't write past the per-thread capture row. */
      if (!s->match_only && inst.offset < s->ncap) {
        CAP(s, cap_slot)[inst.offset] = (int)(sp - s->str);
      }
      pc++;
      continue;

    case RE_BOL:
 /* Ruby's `^` is always line-anchored: it matches at the start of the
    string and after any '\n', independent of the /m flag (which in
    Ruby only makes `.` match '\n', i.e. DOTALL). `\A` (RE_BOT) is the
    absolute-start anchor. */
      /* A '\n' at the very end of the string does not start a new line, so
         `^` must not match there (`"a\n".scan(/^/).size == 1`). */
      if (sp == s->str || (sp > s->str && sp < s->str_end && sp[-1] == '\n')) {
        pc++; continue;
      }
      return;

    case RE_EOL:
 /* Ruby's `$` is always line-anchored: it matches at the end of the
    string and before any '\n', independent of /m. `\z` (RE_EOT) /
    `\Z` (RE_EOTNL) are the absolute-end anchors. */
      if (sp == s->str_end || *sp == '\n') {
        pc++; continue;
      }
      return;

    case RE_BOT:
      if (sp == s->str) { pc++; continue; }
      return;

    case RE_EOT:
      if (sp == s->str_end) { pc++; continue; }
      return;

    case RE_EOTNL:
      if (sp == s->str_end || (sp + 1 == s->str_end && *sp == '\n')) { pc++; continue; }
      return;

    case RE_WBOUND:
      {
        mrb_bool before = (sp > s->str) && re_word_before(s->str, sp, s->str_end, s->binary);
        mrb_bool after = (sp < s->str_end) && re_word_at(sp, s->str_end, s->binary);
        if (before != after) { pc++; continue; }
      }
      return;

    case RE_NWBOUND:
      {
        mrb_bool before = (sp > s->str) && re_word_before(s->str, sp, s->str_end, s->binary);
        mrb_bool after = (sp < s->str_end) && re_word_at(sp, s->str_end, s->binary);
        if (before == after) { pc++; continue; }
      }
      return;

    case RE_MATCH:
      s->matched = TRUE;
      if (s->result_caps) {
        memcpy(s->result_caps, CAP(s, cap_slot), sizeof(int) * s->ncap);
      }
      /* Leftmost-first: this is the highest-priority thread to reach a match
         this step (closures run in priority order), so cut every lower one.
         A surviving higher-priority thread can still match later and override
         this in a subsequent step, which is the correct greedy/longest case. */
      s->cut = TRUE;
      return;

    default:
      break;
    }
    break;
  }

  /* Issue #756: grow the thread list on demand instead of silently
     dropping the new thread. The previous form produced false
     negatives on patterns with many simultaneous alternatives. */
  if (list->count >= list->capa) {
    int new_capa = list->capa * 2;
    re_thread *nt = (re_thread *)realloc(list->threads,
                                         sizeof(re_thread) * new_capa);
    if (!nt) return;  /* OOM: drop the thread, same as before */
    list->threads = nt;
    list->capa = new_capa;
  }
  re_thread *t = &list->threads[list->count++];
  t->pc = pc;
  t->cap_slot = cap_slot;
  t->sp = sp;
}

static int
pike_vm(const mrb_regexp_pattern *pat,
        const char *str, mrb_int len, mrb_int start,
        int *captures, int captures_size, mrb_bool binary)
{
  const char *sp = str + start;
  const char *str_end = str + len;
  int ncap = pat->num_captures * 2;
  if (ncap == 0) ncap = 2;

  int list_capa = RE_LIST_CAPA(pat->code_len, pat->loop_depth);

  mrb_bool match_only = (captures == NULL || captures_size == 0);

  /* Use cached VM state if available (avoids malloc per call) */
  mrb_regexp_pattern *mpat = (mrb_regexp_pattern*)pat;  /* for cache_in_use flag */
  /* The claim has to be atomic where threads are real: the cached VM state
     lives on the compiled PATTERN, and a pattern is shared by every thread
     matching against it. Two threads both read the flag as clear, both took the
     same visited array and thread lists, and corrupted each other's match --
     `scan` came back fragmented or short (#4082). The loser takes the malloc
     path that already exists for re-entrancy. The single-threaded build keeps
     the plain read/write it always had. */
#ifdef SP_THREADS
  mrb_bool use_cache = mpat->cached_visited != NULL &&
                       !__atomic_exchange_n(&mpat->cache_in_use, (mrb_bool)TRUE, __ATOMIC_ACQUIRE);
#else
  mrb_bool use_cache = !mpat->cache_in_use && mpat->cached_visited != NULL;
  if (use_cache) mpat->cache_in_use = TRUE;
#endif

  pike_state s;
  s.pat = pat;
  s.ncap = ncap;
  s.str = str;
  s.str_end = str_end;
  s.matched = FALSE;
  s.oom = FALSE;
  s.match_only = match_only;
  s.binary = binary;
  s.cut = FALSE;
  s.pass_span = RE_PASS_SPAN(pat->loop_depth);
  s.gen = s.pass_span;
  s.key_max = s.gen + s.pass_span - 1;
  /* The capture pool and the result slots are two mallocs per match, and a
     page render matches a few hundred times; a small one lives on the stack
     instead (the pool may still grow off it, see pool_alloc). */
  int cap_pool_sb[128], result_caps_sb[64];
  int cap_pool_on_stack = 0;
  if (match_only) {
    s.pool_capa = 1;
    s.pool_next = 0;
    if ((size_t)ncap <= sizeof cap_pool_sb / sizeof cap_pool_sb[0]) { s.cap_pool = cap_pool_sb; cap_pool_on_stack = 1; }
    else s.cap_pool = (int*)malloc(sizeof(int) * ncap);
    s.result_caps = NULL;
  }
  else {
    s.pool_capa = list_capa * 2;
    s.pool_next = 0;
    if ((size_t)s.pool_capa * ncap <= sizeof cap_pool_sb / sizeof cap_pool_sb[0]) { s.cap_pool = cap_pool_sb; cap_pool_on_stack = 1; }
    else s.cap_pool = (int*)malloc(sizeof(int) * s.pool_capa * ncap);
    s.result_caps = (size_t)ncap <= sizeof result_caps_sb / sizeof result_caps_sb[0] ? result_caps_sb : (int*)malloc(sizeof(int) * ncap);
    memset(s.result_caps, -1, sizeof(int) * ncap);
  }
  s.cap_pool_fixed = cap_pool_on_stack;

  re_threadlist curr, next;
  if (use_cache) {
    s.visited = mpat->cached_visited;
    memset(s.visited, 0, sizeof(uint32_t) * (pat->code_len + 1));
    curr.threads = (re_thread*)mpat->cached_threads[0];
    next.threads = (re_thread*)mpat->cached_threads[1];
    curr.capa = next.capa = mpat->cached_list_capa;
  }
  else {
    s.visited = (uint32_t*)calloc(pat->code_len + 1, sizeof(uint32_t));
    curr.threads = (re_thread*)malloc(sizeof(re_thread) * list_capa);
    next.threads = (re_thread*)malloc(sizeof(re_thread) * list_capa);
    curr.capa = next.capa = list_capa;
  }
  curr.count = next.count = 0;

  for (; sp <= str_end; sp++) {
    if (!s.matched) {
      /* Skip ahead when no active threads */
      if (curr.count == 0) {
        if (pat->prefix_len > 0) {
          const char *skip = skip_to_prefix(pat, sp, str_end);
          if (!skip) break;
          sp = skip;
        }
        else if (pat->has_first_bytes) {
          while (sp < str_end && !FIRST_BYTE_OK(pat, (uint8_t)*sp)) sp++;
          if (sp > str_end) break;
        }
      }
      /* Don't seed a new match attempt at a UTF-8 continuation byte —
         a multi-byte char's interior is not a valid char boundary, and
         starting a thread there mis-decodes the char (e.g. a class-
         match on a stray 0x82 instead of the leader's full codepoint). */
      if (!s.binary && curr.count == 0 && sp < str_end && re_utf8_continuation_p(sp)) {
        continue;
      }
      int slot = 0;
      if (!match_only) {
        if (!pool_alloc(&s, &slot)) break;
        memset(CAP(&s, slot), -1, sizeof(int) * ncap);
      }
      s.gen += s.pass_span;
      s.key_max += s.pass_span;
      s.cut = FALSE;
      add_thread(&s, &curr, 0, slot, sp, s.gen);
      if (s.matched && curr.count == 0) break;
    }

    if (sp >= str_end) break;

    if (!match_only) {
      /* Renumber each live thread's capture slot to its list index so the
         pool can be reset to curr.count. Stage the copies through freshly
         allocated tail slots first: writing straight to CAP(i) would clobber
         a low slot that a later thread (index j > i) still needs to read
         whenever the slot assignment is a non-identity permutation -- which
         happens once alternation reorders threads relative to their slot
         numbers. Tail slots are disjoint from every source slot, and the
         final block copy to the front is disjoint because base >= count. */
      if (curr.count > 0) {
        int base = s.pool_next;
        for (int i = 0; i < curr.count; i++) {
          int dst;
          if (!pool_alloc(&s, &dst)) break;
          memcpy(CAP(&s, dst), CAP(&s, curr.threads[i].cap_slot),
                 sizeof(int) * ncap);
          curr.threads[i].cap_slot = i;
        }
        if (s.oom) break;
        memcpy(&s.cap_pool[0], &s.cap_pool[base * ncap],
               sizeof(int) * ncap * curr.count);
      }
      /* The reclaim belongs to every step, not only one with a thread to
         renumber: a live slot is a thread's, and a step whose closure killed
         them all holds none -- a match keeps what it found in result_caps
         rather than in the pool. Left inside the renumbering, a run of
         positions where nothing survives climbed the pool by a slot a
         position, and the pool is grown by doubling.
         (ported from mruby-regexp 8339cd728) */
      s.pool_next = curr.count;
    }

    s.gen += s.pass_span;
    s.key_max += s.pass_span;
    s.cut = FALSE;
    next.count = 0;

    int ch = (uint8_t)*sp;
    int advance = re_charlen(sp, str_end, s.binary);
    /* Decoded codepoint of the current input char. Identical to `ch`
       for ASCII; lazily decoded only when something downstream actually
       inspects a non-ASCII char class. */
    uint32_t curr_cp = (uint32_t)ch;
    if (!s.binary && advance > 1) {
      int dlen = 0;
      curr_cp = re_decode_char(sp, str_end, &dlen, s.binary);
    }

    for (int i = 0; i < curr.count; i++) {
      re_thread *th = &curr.threads[i];
      if (th->pc >= pat->code_len) continue;
      /* A thread enqueued at sp+advance (RE_CLASS over a multi-byte
         char) waits in the list until the byte-stepped outer sp
         catches up to its own sp. Until then, carry it forward to
         next iteration's curr unchanged. */
      if (th->sp != sp) {
        if (next.count < next.capa) {
          next.threads[next.count++] = *th;
        }
        continue;
      }

      re_inst inst = pat->code[th->pc];
      switch (inst.op) {
      case RE_CHAR:
        if (ch == inst.a) {
          int cp = 0;
          if (!match_only && !pool_copy(&s, th->cap_slot, &cp)) break;
          add_thread(&s, &next, th->pc + 1, cp, sp + 1, s.gen);
        }
        break;

      case RE_ANY:
        if (ch != '\n') {
          int cp = 0;
          if (!match_only && !pool_copy(&s, th->cap_slot, &cp)) break;
          add_thread(&s, &next, th->pc + 1, cp, sp + advance, s.gen);
        }
        break;

      case RE_ANY_NL:
        {
          int cp = 0;
          if (!match_only && !pool_copy(&s, th->cap_slot, &cp)) break;
          add_thread(&s, &next, th->pc + 1, cp, sp + advance, s.gen);
        }
        break;

      case RE_CLASS:
        if (class_match(&pat->classes[inst.a], curr_cp, s.binary)) {
          int cp = 0;
          if (!match_only && !pool_copy(&s, th->cap_slot, &cp)) break;
          add_thread(&s, &next, th->pc + 1, cp, sp + advance, s.gen);
        }
        break;

      case RE_NCLASS:
        if (!class_match(&pat->classes[inst.a], curr_cp, s.binary)) {
          int cp = 0;
          if (!match_only && !pool_copy(&s, th->cap_slot, &cp)) break;
          add_thread(&s, &next, th->pc + 1, cp, sp + advance, s.gen);
        }
        break;

      default:
        break;
      }

      /* A higher-priority thread reached a match while building `next`; the
         remaining (lower-priority) threads in `curr` are cut for this step. */
      if (s.cut) break;
    }

    /* swap curr and next */
    {
      re_threadlist tmp = curr;
      curr = next;
      next = tmp;
    }

    if (s.matched && curr.count == 0) break;
  }

  int ret = 0;
  /* A refused buffer says nothing about the text, so it is not answered from
     the threads that were running when it was refused: a match those hold is
     a smaller question's answer, told from the real one by nothing.
     (ported from mruby-regexp e86ec72c7) */
  if (s.matched && !s.oom) {
    if (captures && s.result_caps) {
      int copy = ncap < captures_size ? ncap : captures_size;
      memcpy(captures, s.result_caps, sizeof(int) * copy);
    }
    ret = ncap > 0 ? ncap : 1;
  }

  if (use_cache) {
#ifdef SP_THREADS
    __atomic_store_n(&mpat->cache_in_use, (mrb_bool)FALSE, __ATOMIC_RELEASE);
#else
    mpat->cache_in_use = FALSE;
#endif
  }
  else {
    free(curr.threads);
    free(next.threads);
    free(s.visited);
  }
  if (s.cap_pool != cap_pool_sb) free(s.cap_pool);
  if (s.result_caps && s.result_caps != result_caps_sb) free(s.result_caps);

  return ret;
}

/*
 * Backtracking engine for patterns with backreferences.
 * Step-limited to prevent ReDoS.
 */

/* What one bt_match() call answers. BT_MATCH is the search having reached
   RE_MATCH, BT_FAIL is every alternative it held having been tried, and
   BT_LIMIT is it giving up at one of the limits before it had either. The
   last says nothing about the text, so it is not read as a branch having
   failed and answered from the alternatives left: that would answer a smaller
   question with a shorter match, a later one or none, told from the real
   answer by nothing. backtrack_exec() stops the whole search at it.

   BT_OK is not one of bt_match()'s answers. It is what bt_push() and bt_log()
   answer when there is nothing to hand up, so that what they answer otherwise
   is bt_match()'s answer as it stands. */
#define BT_FAIL 0
#define BT_MATCH 1
#define BT_LIMIT 2
#define BT_OK 4

/* What taking a choice point does beyond resuming where it points. */
enum re_cp_kind {
  RE_CP_FORK,   /* nothing: a branch the search has not tried yet */
  RE_CP_ITER    /* the branch begins an iteration of the loop its `group`
                   keys, whose record is written when the branch is taken
                   rather than when it was pushed (see bt_iter_begin()) */
};

/* A choice point: an alternative the search has not tried yet, as where the
   input stood (`sp`), which instruction takes the alternative (`pc`) and how
   tall the undo log was when it was pushed (`undo_top`). Backtracking pops
   one, takes back every write logged above its `undo_top` and goes on from
   its `sp` and `pc`. A fork costs one of these rather than the C frame the
   engine used to recurse into, so the C stack a search spends no longer grows
   with the subject. (ported from mruby-regexp 7c6059908) */
typedef struct {
  const char *sp;
  uint32_t pc;
  uint32_t undo_top;
  uint32_t group;   /* the loop RE_CP_ITER names */
  uint8_t kind;
} re_cpoint;

/* An undo record: one write the search must be able to take back, as the slot
   written and what stood in it. The records are a stack of their own beside
   the choice points because what a cut does to each differs: the captures a
   positive lookaround or an atomic group wrote outlive it, so its cut drops
   the choice points and leaves the undo log alone, while a negative
   lookaround, whose captures do not outlive it, unwinds both. */
typedef struct {
  int *slot;
  int old;
} re_undo;

/* Everything one backtrack_exec() call carries. What the search itself holds
   between instructions is only its position and its pc. */
typedef struct {
  const mrb_regexp_pattern *pat;
  const char *str;
  const char *str_end;
  int *captures;
  int ncap;
  int steps;
  int frames;              /* lookaround / atomic nesting, not forks */
  mrb_bool binary;
  const char *gpos;
  /* Per pc, the offset the loop that pc keys was entered at, or -1 while none
     is running. A repetition whose body can match empty has to stop once an
     iteration ends where it began, or it would go round at the same position
     until a limit refused it -- which is what the old engine's recursion
     depth was doing for it by accident, and what stopping here does on
     purpose. The pc that keys a loop is its marked head for e* and its marked
     back edge for e+; see mark_empty_loops().
     (ported from mruby-regexp 3af63799f) */
  int *entered_at;
  /* Both grown lazily: a search starts with neither allocated and pays only
     for the kind of state it holds, so a pattern with no fork and no capture
     in it asks the allocator for nothing. */
  re_cpoint *cp;
  uint32_t cp_top, cp_capa;
  re_undo *undo;
  uint32_t undo_top, undo_capa;
  mrb_bool oom;
} bt_state;

/* Whether the search may hold one more entry of backtracking state. The two
   stacks are counted together: each entry stands for a branch or a write the
   search is still able to take back, and bounding their sum is what bounds
   the state one search holds. */
static mrb_bool
bt_room(const bt_state *m)
{
  return m->cp_top + m->undo_top < (uint32_t)MRB_REGEXP_STACK_LIMIT;
}

/* Doubling is what makes a push amortised constant; the ceiling is what keeps
   MRB_REGEXP_STACK_LIMIT a bound on the memory a search asks for and not only
   on the entries it holds at once, since a stack keeps its capacity for the
   rest of the search. A stack is grown only where bt_room() has just passed,
   so the ceiling always leaves room for the entry being pushed. */
static uint32_t
bt_grow_capa(uint32_t capa)
{
  uint32_t next = capa ? capa * 2 : 16;
  if (next > (uint32_t)MRB_REGEXP_STACK_LIMIT) next = (uint32_t)MRB_REGEXP_STACK_LIMIT;
  return next;
}

/* Push a choice point. BT_OK is the push having happened. */
static int
bt_push(bt_state *m, const char *sp, uint32_t pc, uint8_t kind, uint32_t group)
{
  if (!bt_room(m)) return BT_LIMIT;
  if (m->cp_top >= m->cp_capa) {
    uint32_t capa = bt_grow_capa(m->cp_capa);
    re_cpoint *grown = (re_cpoint *)realloc(m->cp, sizeof(re_cpoint) * capa);
    if (!grown) { m->oom = TRUE; return BT_LIMIT; }
    m->cp = grown;
    m->cp_capa = capa;
  }
  re_cpoint *c = &m->cp[m->cp_top++];
  c->sp = sp;
  c->pc = pc;
  c->undo_top = m->undo_top;
  c->kind = kind;
  c->group = group;
  return BT_OK;
}

/* Write a slot, logging what stood in it so backtracking can take it back.
   The write happens here rather than at the caller so the two cannot drift. */
static int
bt_log(bt_state *m, int *slot, int val)
{
  /* A write of the value already there is nothing to put back, and logging it
     would spend an entry of MRB_REGEXP_STACK_LIMIT on a record that restores
     what is already in the slot. The limit is meant to bound the state that
     DIFFERS and would have to be undone, not the writes a search made.
     (ported from mruby-regexp f5314068d) */
  if (*slot == val) return BT_OK;
  if (!bt_room(m)) return BT_LIMIT;
  if (m->undo_top >= m->undo_capa) {
    uint32_t capa = bt_grow_capa(m->undo_capa);
    re_undo *grown = (re_undo *)realloc(m->undo, sizeof(re_undo) * capa);
    if (!grown) { m->oom = TRUE; return BT_LIMIT; }
    m->undo = grown;
    m->undo_capa = capa;
  }
  re_undo *u = &m->undo[m->undo_top++];
  u->slot = slot;
  u->old = *slot;
  *slot = val;
  return BT_OK;
}

/* Take back every write logged above `top`, newest first, so that a slot
   written more than once lands on what it held before the first of them. */
static void
bt_unwind(bt_state *m, uint32_t top)
{
  while (m->undo_top > top) {
    re_undo *u = &m->undo[--m->undo_top];
    *u->slot = u->old;
  }
}

/* Whether the loop `key` keys is at the end of an iteration that began where
   the search now stands: the record names sp. */
#define ITER_EMPTY(m, key, sp) ((m)->entered_at[key] == (int)((sp) - (m)->str))

/* Record that an iteration of the loop `key` keys begins at sp. The record
   goes on the undo log, so backtracking out of an iteration puts back the
   record of the one it lands in. */
static int
bt_iter_begin(bt_state *m, uint32_t key, const char *sp)
{
  return bt_log(m, &m->entered_at[key], (int)(sp - m->str));
}

/* `end_out`, when non-NULL, reports the position the sub-pattern stopped at:
   the atomic group needs the end of its one committed match, which every other
   caller discards (NULL).

   `cp_floor` and `undo_floor` are the heights the two stacks stood at when
   this call was entered. A lookaround and an atomic group still take a C
   frame each, and a frame must not pop past what it found: running out of
   alternatives down to its floor is this call failing, not the caller's, and
   every answer but a match leaves the two stacks as they were. */
static int
bt_match(bt_state *m, const char *sp, uint32_t pc,
         uint32_t cp_floor, uint32_t undo_floor, const char **end_out);

/* Compare two byte spans ignoring ASCII case. Folding stops at ASCII, like
   every other ignorecase decision in this engine (compile_atom's /i handling
   folds only A-Z and a-z into a class bitmap). (ported from mruby-regexp
   f9adb3017) */
static mrb_bool
memcmp_ci(const char *a, const char *a_end, const char *b, int len, int *used)
{
  const char *b_end = b + len;
  const char *a0 = a;
  while (b < b_end) {
    if (a >= a_end) return FALSE;
    int alen = 0, blen = 0;
    uint32_t ca = re_utf8_decode(a, a_end, &alen);
    uint32_t cb = re_utf8_decode(b, b_end, &blen);
    if (alen < 1) alen = 1;
    if (blen < 1) blen = 1;
    if (re_case_fold(ca) != re_case_fold(cb)) return FALSE;
    a += alen;
    b += blen;
  }
  /* A folded comparison need not consume as many bytes as the captured text
     holds -- U+212A against `k` is three against one -- so report what it did.
     (ported from mruby-regexp 618ba9435) */
  *used = (int)(a - a0);
  return TRUE;
}

/* Where a lookbehind's sub-pattern starts: the text before `sp` that the
   sub-pattern describes. A byte-indexed subject rewinds by the byte count the
   compiler measured; every other one rewinds by CHARACTERS, since a class or a
   `.` inside the lookbehind matches one character of whatever width and a byte
   count would land inside a character. (ported from mruby-regexp 103e1a8bc,
   3df2926a1, bd21fe4aa) */
static const char *
lookbehind_start(const mrb_regexp_pattern *pat, const char *str,
                 const char *str_end, const char *sp, uint32_t pc,
                 mrb_bool binary)
{
  (void)str_end;
  if (binary) {
    int lb_len = pat->code[pc].a;
    /* zero bytes for a sub-pattern that consumes characters means the
       alternation's branches have no single byte width (see
       compute_fixed_len); a byte-indexed subject has no rewind to make. */
    if (lb_len == 0 && pat->code[pc + 1].a > 0) return NULL;
    return (sp - str < lb_len) ? NULL : sp - lb_len;
  }
  int nchars = pat->code[pc + 1].a;
  while (nchars > 0) {
    if (sp <= str) return NULL;
    sp--;
    while (sp > str && re_utf8_continuation_p(sp)) sp--;
    nchars--;
  }
  return sp;
}

static int
bt_match(bt_state *m, const char *sp, uint32_t pc,
         uint32_t cp_floor, uint32_t undo_floor, const char **end_out)
{
  const mrb_regexp_pattern *pat = m->pat;
  const char *str = m->str, *str_end = m->str_end;
  mrb_bool binary = m->binary;
  int *captures = m->captures;
  int ncap = m->ncap;

  /* A lookaround and an atomic group each take one of these. Their nesting is
     a property of the pattern, so the ceiling is small; the fork per iteration
     that used to need a large one is on the heap now. Issue #777. */
  if (++m->frames > MRB_REGEXP_FRAME_LIMIT) { m->frames--; return BT_LIMIT; }

  int answer = BT_FAIL;
  for (;;) {
    if (pc >= pat->code_len) goto backtrack;
    if (++m->steps > MRB_REGEXP_STEP_LIMIT) { answer = BT_LIMIT; goto done; }

    re_inst inst = pat->code[pc];
    switch (inst.op) {
    case RE_CHAR:
      if (sp >= str_end || (uint8_t)*sp != inst.a) goto backtrack;
      sp++; pc++;
      break;

    case RE_ANY:
      if (sp >= str_end || *sp == '\n') goto backtrack;
      sp += re_charlen(sp, str_end, binary); pc++;
      break;

    case RE_ANY_NL:
      if (sp >= str_end) goto backtrack;
      sp += re_charlen(sp, str_end, binary); pc++;
      break;

    case RE_CLASS:
      if (sp >= str_end) goto backtrack;
      {
        int dlen = 0;
        uint32_t cp_ = re_decode_char(sp, str_end, &dlen, binary);
        if (!class_match(&pat->classes[inst.a], cp_, binary)) goto backtrack;
        sp += re_charlen(sp, str_end, binary);
      }
      pc++;
      break;

    case RE_NCLASS:
      if (sp >= str_end) goto backtrack;
      {
        int dlen = 0;
        uint32_t cp_ = re_decode_char(sp, str_end, &dlen, binary);
        if (class_match(&pat->classes[inst.a], cp_, binary)) goto backtrack;
        sp += re_charlen(sp, str_end, binary);
      }
      pc++;
      break;

    case RE_MATCH:
      if (end_out) *end_out = sp;
      answer = BT_MATCH;
      goto done;

    case RE_JMP:
      /* A marked backward jump closes e*, whose body can match empty, and
         returns to its head. entered_at[head] holds where the iteration that
         just ended began: one that ended where it began matched empty, so the
         repetition stops here, taking the head's exit and keeping what the
         iteration captured, as Onigmo's null check does. Otherwise the next
         iteration begins here -- mark_empty_loops() marks the closing edge
         and never the head, so the head has no arm of its own to record it
         from, and the first iteration is the one entered_at is still -1 for,
         which is no offset and so never reads as empty. */
      if (inst.a && inst.offset <= pc) {
        uint32_t head = inst.offset;
        if (ITER_EMPTY(m, head, sp)) { pc = pat->code[head].offset; break; }
        int r = bt_iter_begin(m, head, sp);
        if (r != BT_OK) { answer = r; goto done; }
        pc = head;
        break;
      }
      pc = inst.offset;
      break;

    /* A fork takes the branch it prefers and leaves the other as a choice
       point, where recursing into the preferred one and falling through to
       the other on failure is what used to spend a C frame per iteration. */
    /* mark_empty_loops() marks only a BACKWARD edge, so a marked fork always
       closes a repetition whose body can match empty (e+ and e+?); the head
       of e* / e*? is an unmarked forward fork, and its loop is closed by the
       marked RE_JMP above. */
    case RE_SPLIT:
      /* Greedy fork: pc+1 first, then the jump target. Marked, it closes e+?:
         its target begins the next iteration, unless the one that just ended
         was empty, and then there is only the exit. The iteration is recorded
         as that branch is TAKEN rather than here, since it begins only if it
         is -- which is what RE_CP_ITER carries. */
      {
        int r;
        if (inst.a) {
          if (ITER_EMPTY(m, pc, sp)) { pc++; break; }
          if ((r = bt_push(m, sp, inst.offset, RE_CP_ITER, pc)) != BT_OK) { answer = r; goto done; }
          pc++;
          break;
        }
        if ((r = bt_push(m, sp, inst.offset, RE_CP_FORK, 0)) != BT_OK) { answer = r; goto done; }
        pc++;
      }
      break;

    case RE_SPLITNG:
      /* Non-greedy fork: the jump target first, then pc+1. Marked, it closes
         e+, and the branch it takes now is the one that begins the iteration,
         so the record is written here. */
      {
        int r;
        if (inst.a) {
          if (ITER_EMPTY(m, pc, sp)) { pc++; break; }
          if ((r = bt_push(m, sp, pc + 1, RE_CP_FORK, 0)) != BT_OK ||
              (r = bt_iter_begin(m, pc, sp)) != BT_OK) { answer = r; goto done; }
          pc = inst.offset;
          break;
        }
        if ((r = bt_push(m, sp, pc + 1, RE_CP_FORK, 0)) != BT_OK) { answer = r; goto done; }
        pc = inst.offset;
      }
      break;

    case RE_SAVE:
      {
        int slot = inst.offset;
        if (slot >= ncap) goto backtrack;
        int r = bt_log(m, &captures[slot], (int)(sp - str));
        if (r != BT_OK) { answer = r; goto done; }
        pc++;
      }
      break;

    case RE_BOL:
 /* Ruby `^`: always line-anchored (start of string or after '\n'),
    independent of /m. See the RE_BOL note in the NFA loop above. */
      if (sp != str && !(sp > str && sp < str_end && sp[-1] == '\n')) goto backtrack;
      pc++;
      break;

    case RE_EOL:
 /* Ruby `$`: always line-anchored (end of string or before '\n'),
    independent of /m. */
      if (sp != str_end && *sp != '\n') goto backtrack;
      pc++;
      break;

    case RE_BOT:
      if (sp != str) goto backtrack;
      pc++;
      break;

    case RE_EOT:
      if (sp != str_end) goto backtrack;
      pc++;
      break;

    case RE_WBOUND:
      {
        mrb_bool before = (sp > str) && re_word_before(str, sp, str_end, binary);
        mrb_bool after = (sp < str_end) && re_word_at(sp, str_end, binary);
        if (before == after) goto backtrack;
      }
      pc++;
      break;

    case RE_NWBOUND:
      {
        mrb_bool before = (sp > str) && re_word_before(str, sp, str_end, binary);
        mrb_bool after = (sp < str_end) && re_word_at(sp, str_end, binary);
        if (before != after) goto backtrack;
      }
      pc++;
      break;

    case RE_BACKREF:
      {
        int group = inst.a;
        /* Issue #753: bounds-check the group index against the
           captures array. A backref to a non-existent group (e.g.
           `/(\\1)/`) would read past captures[] and use garbage as
           the start/end. CRuby returns nil (no match) -- match the
           same by failing without dereferencing. */
        if (group * 2 + 1 >= ncap) goto backtrack;
        int gs = captures[group * 2];
        int ge = captures[group * 2 + 1];
        if (gs < 0 || ge < 0) goto backtrack;
        int blen = ge - gs;
        if (!inst.offset && sp + blen > str_end) goto backtrack;
        int used = blen;
        if (inst.offset) {
          if (!memcmp_ci(sp, str_end, str + gs, blen, &used)) goto backtrack;
        }
        else if (memcmp(sp, str + gs, blen) != 0) goto backtrack;
        sp += used;
        pc++;
      }
      break;

    case RE_GPOS:
      /* \G: only the position this search started from matches */
      if (m->gpos && sp != m->gpos) goto backtrack;
      pc++;
      break;

    case RE_ATOMIC:
      {
        /* Run the sub-pattern once, take the end of that first match, and
           continue from it: no alternative inside is ever revisited (#3636).
           Its choice points go with it, which is the cut; what it captured
           stands, so the undo log is left alone. */
        const char *aend = NULL;
        uint32_t cp_mark = m->cp_top;
        int r = bt_match(m, sp, pc + 1, m->cp_top, m->undo_top, &aend);
        m->cp_top = cp_mark;
        if (r == BT_LIMIT) { answer = r; goto done; }
        if (r != BT_MATCH || !aend) goto backtrack;
        sp = aend;
        pc = inst.offset;
      }
      break;

    case RE_LOOKAHEAD:
      {
        uint32_t cp_mark = m->cp_top;
        int r = bt_match(m, sp, pc + 1, m->cp_top, m->undo_top, NULL);
        m->cp_top = cp_mark;
        if (r == BT_LIMIT) { answer = r; goto done; }
        if (r != BT_MATCH) goto backtrack;
        pc = inst.offset;
      }
      break;

    case RE_NEG_LOOKAHEAD:
      {
        uint32_t cp_mark = m->cp_top, undo_mark = m->undo_top;
        int r = bt_match(m, sp, pc + 1, m->cp_top, m->undo_top, NULL);
        m->cp_top = cp_mark;
        /* the assertion holding is the sub-pattern having failed, and what a
           failed sub-pattern wrote is not the match's: unwind it too */
        bt_unwind(m, undo_mark);
        if (r == BT_LIMIT) { answer = r; goto done; }
        if (r == BT_MATCH) goto backtrack;
        pc = inst.offset;
      }
      break;

    case RE_LOOKBEHIND:
      {
        const char *back = lookbehind_start(pat, str, str_end, sp, pc, binary);
        if (!back) goto backtrack;  /* not enough text before */
        uint32_t cp_mark = m->cp_top;
        int r = bt_match(m, back, pc + 2, m->cp_top, m->undo_top, NULL);
        m->cp_top = cp_mark;
        if (r == BT_LIMIT) { answer = r; goto done; }
        if (r != BT_MATCH) goto backtrack;
        pc = inst.offset;
      }
      break;

    case RE_NEG_LOOKBEHIND:
      {
        const char *back = lookbehind_start(pat, str, str_end, sp, pc, binary);
        if (back) {
          uint32_t cp_mark = m->cp_top, undo_mark = m->undo_top;
          int r = bt_match(m, back, pc + 2, m->cp_top, m->undo_top, NULL);
          m->cp_top = cp_mark;
          bt_unwind(m, undo_mark);
          if (r == BT_LIMIT) { answer = r; goto done; }
          if (r == BT_MATCH) goto backtrack;
        }
        /* if not enough text before, negative lookbehind succeeds */
        pc = inst.offset;
      }
      break;

    default:
      goto backtrack;
    }
    continue;

  backtrack:
    /* Every alternative this call holds having been tried is this call
       failing. The floor is the caller's state, which is not this call's to
       pop. */
    if (m->cp_top <= cp_floor) { answer = BT_FAIL; goto done; }
    {
      re_cpoint c = m->cp[--m->cp_top];
      bt_unwind(m, c.undo_top);
      sp = c.sp;
      pc = c.pc;
      if (c.kind == RE_CP_ITER) {
        int r = bt_iter_begin(m, c.group, sp);
        if (r != BT_OK) { answer = r; goto done; }
      }
    }
  }

done:
  m->frames--;
  /* Every answer but a match leaves the two stacks as this call found them,
     so no pop of the caller's unwinds a write it restores itself. */
  if (answer != BT_MATCH) {
    m->cp_top = cp_floor;
    bt_unwind(m, undo_floor);
  }
  return answer;
}

static int
backtrack_exec(const mrb_regexp_pattern *pat,
               const char *str, mrb_int len, mrb_int start,
               int *captures, int captures_size, mrb_bool binary)
{
  const char *str_end = str + len;
  int ncap = pat->num_captures * 2;
  if (ncap == 0) ncap = 2;

  /* One block for the capture slots and the per-pc iteration records, so a
     search asks the allocator once for the state whose size the pattern
     fixes; the two stacks, whose size the subject fixes, grow on their own. */
  int caps_sb[256];
  int *caps = (size_t)(ncap + (int)pat->code_len) <= sizeof caps_sb / sizeof caps_sb[0]
              ? caps_sb : (int*)malloc(sizeof(int) * (ncap + (int)pat->code_len));
  if (!caps) return 0;

  bt_state m;
  memset(&m, 0, sizeof m);
  m.entered_at = caps + ncap;
  m.pat = pat;
  m.str = str;
  m.str_end = str_end;
  m.captures = caps;
  m.ncap = ncap;
  m.binary = binary;
  m.gpos = str + start;

  int ret = 0;
  for (const char *sp = str + start; sp <= str_end; sp++) {
    /* Skip ahead using literal prefix or first-byte bitmap */
    if (pat->prefix_len > 0) {
      const char *skip = skip_to_prefix(pat, sp, str_end);
      if (!skip) break;
      sp = skip;
    }
    else if (pat->has_first_bytes) {
      while (sp < str_end && !FIRST_BYTE_OK(pat, (uint8_t)*sp)) sp++;
      if (sp > str_end) break;
    }
    /* Don't seed a match attempt at a UTF-8 continuation byte: a
       multi-byte char's interior is not a valid char boundary (same
       rule as the Pike VM's seed loop). */
    if (!binary && sp < str_end && re_utf8_continuation_p(sp)) {
      continue;
    }
    memset(caps, -1, sizeof(int) * ncap);
    /* The iteration records are cleared with them: an undo log does not
       unwind on success, so a repetition at a later start position would
       otherwise stop on a record the position before left at the same
       offset. */
    memset(m.entered_at, -1, sizeof(int) * pat->code_len);
    /* The step count is per start position, as it was when it lived in a
       local of this loop; the two stacks start empty at each, every previous
       attempt having unwound to its floor. */
    m.steps = 0;
    m.cp_top = 0;
    m.undo_top = 0;

    int r = bt_match(&m, sp, 0, 0, 0, NULL);
    if (r == BT_MATCH) {
      if (captures) {
        int copy = ncap < captures_size ? ncap : captures_size;
        memcpy(captures, caps, sizeof(int) * copy);
      }
      ret = ncap > 0 ? ncap : 1;
      break;
    }
    /* A limit says nothing about the text, so the search stops rather than
       going on to a later start position and answering with whatever it
       finds there: that would be a different match reported as this one. */
    if (r == BT_LIMIT) break;
  }
  if (caps != caps_sb) free(caps);
  free(m.cp);
  free(m.undo);
  return ret;
}

/* Fast path for pure literal patterns: use memchr+memcmp, no NFA needed */
static int
literal_exec(const mrb_regexp_pattern *pat,
             const char *str, mrb_int len, mrb_int start,
             int *captures, int captures_size)
{
  const char *sp = str + start;
  const char *str_end = str + len;
  int plen = pat->prefix_len;

  while (sp + plen <= str_end) {
    const char *found = (const char*)memchr(sp, pat->prefix[0], str_end - sp);
    if (!found || found + plen > str_end) return 0;
    if (plen == 1 || memcmp(found + 1, pat->prefix + 1, plen - 1) == 0) {
      /* match found */
      if (captures && captures_size >= 2) {
        captures[0] = (int)(found - str);
        captures[1] = (int)(found - str) + plen;
      }
      return 2;  /* group 0 start/end */
    }
    sp = found + 1;
  }
  return 0;
}

/* Public entry point */
int
re_exec(const mrb_regexp_pattern *pat,
        const char *str, mrb_int len, mrb_int start,
        int *captures, int captures_size, mrb_bool binary)
{
  if (pat->is_literal) {
    return literal_exec(pat, str, len, start, captures, captures_size);
  }
  if (pat->has_backref || pat->needs_backtrack) {
    return backtrack_exec(pat, str, len, start, captures, captures_size, binary);
  }
  return pike_vm(pat, str, len, start, captures, captures_size, binary);
}

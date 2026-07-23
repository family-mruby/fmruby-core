/* sp_enum.h -- the Enumerator handle (cursor/generator ops in lib/sp_cold.c).
 *
 * Two flavors: a materialized snapshot (items + cursor, from a collection's
 * blockless #each) or a fiber-backed generator (Enumerator.new { |y| ... },
 * where `y << v` is a Fiber.yield). The fiber is created lazily on first
 * #next and re-created on #rewind. */
#ifndef SP_ENUM_H
#define SP_ENUM_H

#include "sp_alloc.h"
#include "sp_fiber.h"

typedef struct {
  sp_PolyArray *items; mrb_int cursor;   /* materialized mode (items != NULL) */
  void (*gen)(sp_Fiber *);                /* generator body (fiber mode, gen != NULL) */
  void *gen_cap;                          /* captures, passed via fiber user_data */
  sp_Fiber *fib;                          /* current generator fiber (lazy) */
  mrb_bool peeked; sp_RbVal peek_val;     /* #peek lookahead cache */
  sp_RbVal size;                          /* #size for a generator: a value, a
                                             callable (Proc), or nil. Unused by
                                             the materialized path (items->len). */
  sp_RbVal feed; mrb_bool has_feed;       /* #feed: value returned by the next Fiber.yield */
  sp_RbVal gen_result;                    /* generator body's return -> StopIteration#result */
  sp_RbVal source;                        /* the iterated receiver -> materialized StopIteration#result */
  const char *meth;                       /* creating method name, for #inspect ("each", ...) */
  mrb_bool gen_label;                     /* #inspect as a Generator wrapper (chunk_while & co.):
                                             the items are an eager snapshot, but CRuby shows
                                             #<Enumerator: #<Enumerator::Generator:0x..>:each> */
  mrb_bool frozen;                        /* Object#freeze observed (sp_gc_alloc zero-fills) */
  mrb_bool is_chain;                      /* built by Enumerable#chain / Enumerator#+: the items
                                             are the concatenated sources, and #class reports
                                             Enumerator::Chain (sp_gc_alloc zero-fills) */
} sp_Enumerator;

#endif

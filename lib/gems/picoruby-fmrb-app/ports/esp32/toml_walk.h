#pragma once

/* A parsed TOML tree (tomlc99) handed to a visitor, value by value.
 *
 * The walk is pure C over tomlc99 so the same code serves the Ruby binding
 * (toml.c, which builds Hash/Array) and the host test (test/toml/dump.c,
 * which prints a Ruby literal). What a value turns into is decided here,
 * once, for both:
 *   table               -> begin_table ... end_table (keys in file order)
 *   array, array of tables -> begin_array ... end_array
 *   "string" / 'literal' -> string (escapes resolved)
 *   true / false        -> boolean
 *   integer (0x.., 0o.., 0b.., 1_000) -> integer
 *   float               -> real
 *   anything else that parsed (a date, or one unquoted word such as
 *   `bg = black`)       -> string, the text as written
 */

#include <stddef.h>
#include <stdint.h>
#include "toml.h"

typedef struct {
    void (*begin_table)(void *user);
    void (*end_table)(void *user);
    void (*begin_array)(void *user);
    void (*end_array)(void *user);
    /* The key the next value (or container) belongs to, inside a table.
       Valid until that value has been reported. */
    void (*key)(void *user, const char *key);
    void (*string)(void *user, const char *s, size_t len);
    void (*integer)(void *user, int64_t v);
    void (*real)(void *user, double v);
    void (*boolean)(void *user, int v);
} fmrb_toml_visitor_t;

/* Deepest nesting walked; anything below it is left out. */
#define FMRB_TOML_WALK_MAX_DEPTH 16

/* Reports root as one table. xfree releases the strings tomlc99 hands out
   (it must be the free that matches the allocator tomlc99 was given). */
void fmrb_toml_walk(const toml_table_t *root, const fmrb_toml_visitor_t *v,
                    void *user, void (*xfree)(void *));

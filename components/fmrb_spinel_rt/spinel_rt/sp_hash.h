#ifndef SP_HASH_H
#define SP_HASH_H
/* sp_hash.h -- cold CRUD-op surface for the 4 non-poly-valued typed hashes
 * (sp_StrIntHash / sp_StrStrHash / sp_IntStrHash / sp_IntIntHash).
 *
 * The struct layouts already live in sp_types.h (shared). None of these
 * four hash types appear in optcarrot's hot loop (0 uses for 3 of them,
 * 4 non-hot uses for IntStrHash -- unlike SymPolyHash/PolyPolyHash, which
 * stay in spinel_rt.h because optcarrot calls their accessors hundreds of
 * times per frame), so the full CRUD surface -- not just the cold
 * materializers -- compiles once into libspinel_rt.a (lib/sp_hash.c)
 * instead of into every generated TU.
 *
 * inspect/to_a/invert functions that bridge to a poly-valued hash
 * (sp_StrPolyHash/sp_PolyPolyHash) or sp_PolyArray stay in spinel_rt.h --
 * moving those would require exposing the poly cluster across the archive
 * boundary, which is excluded (de-inlining poly accessors regresses
 * optcarrot fps ~15%, per project_sp_runtime_lib_eviction).
 */
#include "sp_types.h"   /* sp_StrIntHash / sp_StrStrHash / sp_IntStrHash / sp_IntIntHash */
#include "sp_gc.h"      /* sp_gc_alloc, SP_GC_ROOT, sp_mark_string */
#include "sp_array.h"   /* sp_StrArray / sp_IntArray new/push (keys/values) */
#include "sp_str.h"     /* sp_str_hash / sp_str_eq / _sp_istr_idx */
#include "sp_inspect.h" /* sp_inspect_container for the #inspect wrappers */
#include "sp_string.h"  /* sp_String builder for sp_IntIntHash_inspect */
#include "sp_re.h"      /* mrb_regexp_pattern for sp_re_gsub_str_str_hash/sub_str_str_hash */

void sp_StrIntHash_fin(void*p);
void sp_StrIntHash_scan(void*p);
sp_StrIntHash*sp_StrIntHash_new(void);
sp_StrIntHash*sp_StrIntHash_new_with_default(sp_int d);
void sp_StrIntHash_grow(sp_StrIntHash*h);
sp_int sp_StrIntHash_get(sp_StrIntHash*h,const char*k);
sp_int sp_StrIntHash_get_opt(sp_StrIntHash*h,const char*k);
sp_int sp_StrIntHash_fetch_or(sp_StrIntHash*h,const char*k,sp_int d);
void sp_StrIntHash_set(sp_StrIntHash*h,const char*k,sp_int v);
sp_bool sp_StrIntHash_has_key(sp_StrIntHash*h,const char*k);
sp_bool sp_StrIntHash_has_value(sp_StrIntHash*h,sp_int v);
sp_int sp_StrIntHash_length(sp_StrIntHash*h);
void sp_StrIntHash_delete(sp_StrIntHash*h,const char*k);
sp_StrArray*sp_StrIntHash_keys(sp_StrIntHash*h);
sp_IntArray*sp_StrIntHash_values(sp_StrIntHash*h);
sp_StrIntHash*sp_StrIntHash_merge(sp_StrIntHash*a,sp_StrIntHash*b);
void sp_StrIntHash_update(sp_StrIntHash*a,sp_StrIntHash*b);
sp_StrIntHash*sp_StrIntHash_dup(sp_StrIntHash*h);
sp_StrIntHash*sp_StrIntHash_replace(sp_StrIntHash*h,sp_StrIntHash*o);
void sp_StrIntHash_clear(sp_StrIntHash*h);
sp_bool sp_StrIntHash_eq(sp_StrIntHash*a,sp_StrIntHash*b);
void sp_StrStrHash_fin(void*p);
void sp_StrStrHash_scan(void*p);
sp_StrStrHash*sp_StrStrHash_new(void);
sp_StrStrHash*sp_StrStrHash_new_with_default(const char*d);
void sp_StrStrHash_grow(sp_StrStrHash*h);
const char*sp_StrStrHash_get(sp_StrStrHash*h,const char*k);
void sp_StrStrHash_set(sp_StrStrHash*h,const char*k,const char*v);
sp_bool sp_StrStrHash_has_key(sp_StrStrHash*h,const char*k);
sp_bool sp_StrStrHash_has_value(sp_StrStrHash*h,const char*v);
sp_int sp_StrStrHash_length(sp_StrStrHash*h);
void sp_StrStrHash_delete(sp_StrStrHash*h,const char*k);
sp_StrArray*sp_StrStrHash_keys(sp_StrStrHash*h);
sp_StrArray*sp_StrStrHash_values(sp_StrStrHash*h);
sp_StrStrHash*sp_StrStrHash_invert(sp_StrStrHash*h);
void sp_StrStrHash_update(sp_StrStrHash*a,sp_StrStrHash*b);
sp_StrStrHash*sp_StrStrHash_dup(sp_StrStrHash*h);
sp_StrStrHash*sp_StrStrHash_merge(sp_StrStrHash*a,sp_StrStrHash*b);
sp_StrStrHash*sp_StrStrHash_replace(sp_StrStrHash*h,sp_StrStrHash*o);
void sp_StrStrHash_clear(sp_StrStrHash*h);
sp_bool sp_StrStrHash_eq(sp_StrStrHash*a,sp_StrStrHash*b);
void sp_IntStrHash_fin(void*p);
void sp_IntStrHash_scan(void*p);
sp_IntStrHash*sp_IntStrHash_new(void);
sp_IntStrHash*sp_IntStrHash_new_with_default(const char*d);
void sp_IntStrHash_grow(sp_IntStrHash*h);
void sp_IntStrHash_set(sp_IntStrHash*h,sp_int k,const char*v);
const char*sp_IntStrHash_get(sp_IntStrHash*h,sp_int k);
sp_IntStrHash*sp_IntStrHash_merge(sp_IntStrHash*a,sp_IntStrHash*b);
sp_bool sp_IntStrHash_has_key(sp_IntStrHash*h,sp_int k);
sp_bool sp_IntStrHash_has_value(sp_IntStrHash*h,const char*v);
sp_int sp_IntStrHash_length(sp_IntStrHash*h);
sp_IntArray*sp_IntStrHash_keys(sp_IntStrHash*h);
sp_StrArray*sp_IntStrHash_values(sp_IntStrHash*h);
sp_IntStrHash*sp_IntStrHash_dup(sp_IntStrHash*h);
sp_IntStrHash*sp_IntStrHash_replace(sp_IntStrHash*h,sp_IntStrHash*o);
sp_bool sp_IntStrHash_eq(sp_IntStrHash*a,sp_IntStrHash*b);
void sp_IntIntHash_fin(void*p);
sp_IntIntHash*sp_IntIntHash_new(void);
sp_IntIntHash*sp_IntIntHash_new_with_default(sp_int d);
void sp_IntIntHash_grow(sp_IntIntHash*h);
void sp_IntIntHash_set(sp_IntIntHash*h,sp_int k,sp_int v);
sp_int sp_IntIntHash_get(sp_IntIntHash*h,sp_int k);
sp_IntIntHash*sp_IntIntHash_merge(sp_IntIntHash*a,sp_IntIntHash*b);
void sp_IntIntHash_delete(sp_IntIntHash*h,sp_int k);
void sp_IntStrHash_delete(sp_IntStrHash*h,sp_int k);
sp_int sp_IntIntHash_get_opt(sp_IntIntHash*h,sp_int k);
sp_int sp_IntIntHash_fetch_or(sp_IntIntHash*h,sp_int k,sp_int d);
sp_bool sp_IntIntHash_has_key(sp_IntIntHash*h,sp_int k);
sp_int sp_IntIntHash_length(sp_IntIntHash*h);
sp_IntArray*sp_IntIntHash_keys(sp_IntIntHash*h);
sp_IntArray*sp_IntIntHash_values(sp_IntIntHash*h);
sp_bool sp_IntIntHash_has_value(sp_IntIntHash*h,sp_int v);
sp_bool sp_IntIntHash_eq(sp_IntIntHash*a,sp_IntIntHash*b);
sp_IntIntHash*sp_IntIntHash_dup(sp_IntIntHash*h);
sp_IntIntHash*sp_IntIntHash_replace(sp_IntIntHash*h,sp_IntIntHash*o);
void sp_IntIntHash_clear(sp_IntIntHash*h);
const char*sp_StrIntHash_inspect(sp_StrIntHash*h);
sp_int sp_StrIntHash_proc_fn(void *cap, sp_int argc, sp_int *args);
const char*sp_StrStrHash_inspect(sp_StrStrHash*h);
const char*sp_IntStrHash_inspect(sp_IntStrHash*h);
const char*sp_IntIntHash_inspect(sp_IntIntHash*h);

sp_PolyArray*sp_StrIntHash_to_a(sp_StrIntHash*h);
sp_PolyArray*sp_StrStrHash_to_a(sp_StrStrHash*h);
sp_PolyArray*sp_IntStrHash_to_a(sp_IntStrHash*h);

/* ---- sp_str_sub_str_str_hash relocated from spinel_rt.h (0 optcarrot uses). ---- */
const char *sp_str_sub_str_str_hash(const char *str, const char *pat, sp_StrStrHash *h);

/* ---- Regexp#gsub/#sub with a replacement Hash: relocated from
   spinel_rt.h (0 optcarrot uses). Needs mrb_regexp_pattern/re_exec
   (sp_re.h, included by lib/sp_cold.c already). ---- */
const char *sp_re_gsub_str_str_hash(mrb_regexp_pattern *pat, const char *str, sp_StrStrHash *h);
const char *sp_re_sub_str_str_hash(mrb_regexp_pattern *pat, const char *str, sp_StrStrHash *h);

/* ---- sp_IntStrHash_clear: missed in the original CRUD batch
   (present for the other 3 hash types, overlooked for this one). ---- */
void sp_IntStrHash_clear(sp_IntStrHash*h);

#endif

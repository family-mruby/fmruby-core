#ifndef SP_RANGE_H
#define SP_RANGE_H
/* sp_range.h -- Range value-type helpers (see sp_types.h for sp_Range).
 *
 * sp_range_new / _to_ia are called from optcarrot's hot path (range
 * literals / iteration), so they stay `static inline` here -- each
 * generated TU still compiles its own copy, identical to when they lived
 * directly in spinel_rt.h. sp_range_new_step / _eq are trivial
 * single-expression constructors; marking them inline too is effectively a
 * no-op (GCC already inlines such leaf functions at -O2) and keeps the
 * whole small cluster together instead of splitting by a usage-count
 * threshold that could drift under future edits.
 *
 * sp_range_include / sp_range_str see 0 optcarrot uses and have never been
 * inline, so their bodies compile once into libspinel_rt.a (lib/sp_cold.c).
 */
#include "sp_types.h"   /* sp_Range */
#include "sp_array.h"   /* sp_IntArray_from_range / _from_range_step */

static inline sp_Range sp_range_new(sp_int f,sp_int l,sp_int e){sp_Range r;r.first=f;r.last=l;r.excl=e;r.step=0;return r;}
static inline sp_Range sp_range_new_step(sp_int f,sp_int l,sp_int e,sp_int s){sp_Range r;r.first=f;r.last=l;r.excl=e;r.step=s;return r;}
/* The effective stride: a literal `a..b` range stores 0, which iterates by +1. */
static inline sp_int sp_range_step(sp_Range r){return r.step==0?1:r.step;}
/* Number of elements the range enumerates (0 for an empty one), honoring step. */
static inline sp_int sp_range_count(sp_Range r){
  sp_int s=sp_range_step(r);
  sp_int lastv=r.excl?(r.last-(s>0?1:-1)):r.last;
  sp_int n=(lastv-r.first)/s+1;
  return n<0?0:n;
}
/* Materialize the range into an int array (ascending or descending per step).
   The +1 stride (every literal `a..b` range) keeps the tight from_range loop; a
   real step only appears for downto / explicit step, so it pays the general
   path only then. */
static inline sp_IntArray *sp_range_to_ia(sp_Range r){
  /* an endless range cannot materialize (CRuby raises instead of hanging) */
  if(r.last==INTPTR_MAX)sp_raise_cls("RangeError","cannot convert endless range to an array");
  if(r.first==INTPTR_MIN)sp_raise_cls("TypeError","can't iterate from NilClass");
  sp_int s=sp_range_step(r);
  if(s==1)return sp_IntArray_from_range(r.first,r.last-r.excl);
  return sp_IntArray_from_range_step(r.first,r.last,s,r.excl);
}
/* Last enumerated element (== first for an empty range), and the min/max of the
   enumerated set -- direction-aware, so a descending range reports them right. */
static inline sp_int sp_range_last_elem(sp_Range r){
  sp_int n=sp_range_count(r);
  return n<=0?r.first:r.first+(n-1)*sp_range_step(r);
}
/* min/max of an EMPTY (backwards, or exclusive single-point) range is nil
   (SP_INT_NIL, the nullable-int sentinel) -- CRuby returns nil there. A
   descending step range (5.downto(1)) still enumerates, so only a
   positive-step empty span is nil (#2412). */
static inline sp_int sp_range_min_v(sp_Range r){
  /* a beginless range has no minimum; an endless one's is its begin (#3668) */
  if(r.first==INTPTR_MIN)sp_raise_cls("RangeError","cannot get the minimum of beginless range");
  if(r.last==INTPTR_MAX)return r.first;
  if(sp_range_count(r)<=0)return SP_INT_NIL; sp_int a=r.first,b=sp_range_last_elem(r); return a<b?a:b; }
static inline sp_int sp_range_max_v(sp_Range r){
  /* a beginless range's maximum is its end; an endless one has none (#3668) */
  if(r.last==INTPTR_MAX)sp_raise_cls("RangeError","cannot get the maximum of endless range");
  if(r.first==INTPTR_MIN)return r.excl?r.last-1:r.last;
  if(sp_range_count(r)<=0)return SP_INT_NIL; sp_int a=r.first,b=sp_range_last_elem(r); return a>b?a:b; }
static inline sp_bool sp_range_eq(sp_Range a,sp_Range b){return a.first==b.first&&a.last==b.last&&a.excl==b.excl;}

sp_bool sp_range_include(sp_Range *r, sp_int x);
const char *sp_range_str(sp_Range r);
const char *sp_range_inspect(sp_Range r);

/* Float/String Range value-type ops -- 0 optcarrot uses, bodies in
   lib/sp_cold.c (see sp_types.h for sp_FloatRange/sp_StrRange). */
sp_FloatRange sp_frange_new(sp_float f, sp_float l, sp_int e);
sp_FloatRange sp_frange_new_o(sp_float f, sp_float l, sp_int e, sp_int om);
sp_bool sp_frange_cover(sp_FloatRange r, sp_float x);
sp_bool sp_frange_eq(sp_FloatRange a, sp_FloatRange b);
const char *sp_frange_inspect(sp_FloatRange r);
sp_RbVal sp_box_frange(sp_FloatRange v);
sp_float sp_frange_max(sp_FloatRange r);
sp_StrRange sp_srange_new(const char *f, const char *l, sp_int e);
sp_StrArray *sp_srange_to_a(sp_StrRange r);
sp_bool sp_srange_eq(sp_StrRange a, sp_StrRange b);
sp_bool sp_srange_cover(sp_StrRange r, const char *x);
const char *sp_srange_to_s(sp_StrRange r);
const char *sp_srange_inspect(sp_StrRange r);
sp_RbVal sp_box_srange(sp_StrRange v);

#endif

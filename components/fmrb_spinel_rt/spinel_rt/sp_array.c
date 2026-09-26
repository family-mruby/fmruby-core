/* sp_array.c -- cold typed-array operations (see sp_array.h).
 *
 * The hot accessors (new / push / pop / shift / get / set / length /
 * empty) are inline in sp_array.h and compiled per generated TU. The
 * out-of-line ops below -- sort / slice / dup / set algebra / join /
 * inspect-free reductions -- are cold (never on optcarrot's inner loop),
 * so they compile once into libspinel_rt.a instead of into every TU.
 *
 * They reach the GC string heap via sp_alloc.h and call back into the
 * inline core through sp_array.h; sp_sprintf / sp_raise_* are resolved
 * at the final link against the generated TU. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include "sp_array.h"
#include "sp_random.h"  /* sp_krand_below for shuffle/sample */
#include "sp_string.h"   /* sp_String builder for join/to_poly */
#include "sp_inspect.h"  /* sp_inspect_container for the #inspect wrappers */
#include "sp_str.h"      /* sp_str_succ for from_string_range */

/* Issue #799: clamp e-s+1 against size_t overflow + an arbitrary sanity
   cap (1 << 30 elements; ~8 GB at 8 bytes/elem). Without the cap,
   `(1..INTPTR_MAX).to_a` overflows the realloc size_t to a tiny number,
   then writes past the allocation. */
sp_IntArray*sp_IntArray_from_range(sp_int s,sp_int e){sp_IntArray*a=sp_IntArray_new();sp_int n=e-s+1;if(n<0)n=0;if(n>(sp_int)(1LL<<30))n=(sp_int)(1LL<<30);if(n>a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*a->cap);h->size-=sizeof(sp_int)*a->cap;a->cap=n;a->data=(sp_int*)sp_pl_realloc(a->data,sizeof(sp_int)*a->cap);h->size+=sizeof(sp_int)*a->cap;sp_gc_bytes_add(sizeof(sp_int)*a->cap);}for(sp_int i=0;i<n;i++)a->data[i]=s+i;a->len=n;return a;}
/* (beg..end).step(step) materialised as an IntArray. step==0 raises like
   CRuby; a negative step descends; exclusive ranges drop the endpoint. The
   loop tests the bound directly and guards v+=step, so a range near the
   sp_int limits (e.g. an endless range's INTPTR_MAX end) cannot overflow --
   signed integer overflow is undefined behaviour in C. */
sp_IntArray*sp_IntArray_from_range_step(sp_int beg,sp_int end,sp_int step,sp_int excl){
  if(step==0)sp_raise_cls("ArgumentError","step can't be 0");
  /* An endless range reaches here as INTPTR_MAX, so the loop below walks the
     whole integer line and the process hangs. The float variant already
     refuses a span it cannot materialise; do the same here, counting in
     unsigned arithmetic so the span of an endless range cannot overflow.
     CRuby answers a lazy sequence, which spinel does not have. */
  { int forward = step > 0 ? (beg < end || (!excl && beg == end))
                           : (beg > end || (!excl && beg == end));
    uintptr_t span = step > 0 ? (uintptr_t)end - (uintptr_t)beg
                             : (uintptr_t)beg - (uintptr_t)end;
    uintptr_t mag  = step > 0 ? (uintptr_t)step : -(uintptr_t)step;
    if(forward && span / mag >= (uintptr_t)(1LL<<30))
      sp_raise_cls("RangeError","range too large to materialize"); }
  sp_IntArray*a=sp_IntArray_new();
  if(step>0){
    for(sp_int v=beg;v<end||(!excl&&v==end);){
      sp_IntArray_push(a,v);
      if(v>0&&step>INTPTR_MAX-v)break;
      v+=step;
    }
  }
  else{
    for(sp_int v=beg;v>end||(!excl&&v==end);){
      sp_IntArray_push(a,v);
      if(v<0&&step<INTPTR_MIN-v)break;
      v+=step;
    }
  }
  return a;
}
sp_IntArray*sp_IntArray_dup(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*b=sp_IntArray_new();if(a->len>b->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)b-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*b->cap);h->size-=sizeof(sp_int)*b->cap;b->cap=a->len;void*nd=sp_pl_realloc(b->data,sizeof(sp_int)*b->cap);if(!nd)sp_oom_die();b->data=(sp_int*)nd;h->size+=sizeof(sp_int)*b->cap;sp_gc_bytes_add(sizeof(sp_int)*b->cap);}memcpy(b->data,a->data+a->start,sizeof(sp_int)*a->len);b->len=a->len;return b;}
/* a[start, len] / a[start..end] for IntArray. Negative start counts from
   the end. start past the array length yields an empty result; len is
   clamped so we never read past the source. CRuby returns nil for
   out-of-bounds start; we return an empty IntArray since this typed
   collection has no nullable form. */
/* Slice length clamp, shared shape across every *_slice below and in
   sp_str.c / spinel_rt.h: written as `len > N - start`, never
   `start + len > N`. Ruby hands the length straight through from user code,
   so `a[1, 2**63-1]` reaches here with len == INT64_MAX; the sum then
   overflows, wraps negative, the guard reads FALSE, and the clamp is skipped
   entirely. The copy loop after it is bounded only by len, so it walks off the
   heap -- a segfault today on ordinary -O2 for an index CRuby answers fine.
   By this point start is already known to be in [0, N), so N - start cannot
   underflow. */
sp_IntArray*sp_IntArray_slice(sp_IntArray*a,sp_int start,sp_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_IntArray*b=sp_IntArray_new();if(start>=a->len||len<=0)return b;if(len>a->len-start)len=a->len-start;if(len>b->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)b-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*b->cap);h->size-=sizeof(sp_int)*b->cap;b->cap=len;b->data=(sp_int*)sp_pl_realloc(b->data,sizeof(sp_int)*b->cap);h->size+=sizeof(sp_int)*b->cap;sp_gc_bytes_add(sizeof(sp_int)*b->cap);}memcpy(b->data,a->data+a->start+start,sizeof(sp_int)*len);b->len=len;return b;}
/* a[start..end] / a[start...end] with possibly negative endpoints.
   Normalize end against a->len first; the bare _slice already handles
   negative start. Issue #496. */
sp_IntArray*sp_IntArray_slice_range(sp_IntArray*a,sp_int start,sp_int end_,sp_int excl){SP_GC_ROOT(a);if(end_<0)end_+=a->len;if(start<0)start+=a->len;if(start<0||start>a->len)return NULL;/* a start outside [-len, len] is nil (#4524); start == len is [] */sp_int n=end_-start+(excl?0:1);if(n<0)n=0;return sp_IntArray_slice(a,start,n);}
/* `a.replace(a)` is a no-op in Ruby. Truncating first and copying after made
   it empty the array instead -- the source is the destination, so its length
   was already zero by the time the copy read it. The poly form takes the same
   guard.

   And a FROZEN receiver raises before the truncation, not after: every other
   mutator here checks, but replace did not, so `[1, 2].freeze.replace([3, 4])`
   quietly rewrote it. */
void sp_IntArray_replace(sp_IntArray*dst,sp_IntArray*src){if(dst==src)return;if(dst->frozen){sp_raise_frozen_array_at(dst,SP_BUILTIN_INT_ARRAY);return;}dst->len=0;dst->start=0;if(src->len>dst->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)dst-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*dst->cap);h->size-=sizeof(sp_int)*dst->cap;void*nd=sp_pl_realloc(dst->data,sizeof(sp_int)*src->len);if(!nd){perror("realloc");exit(1);}dst->data=(sp_int*)nd;dst->cap=src->len;h->size+=sizeof(sp_int)*dst->cap;sp_gc_bytes_add(sizeof(sp_int)*dst->cap);}memcpy(dst->data,src->data+src->start,sizeof(sp_int)*src->len);dst->len=src->len;}
/* arr[start,len] = src / arr[range] = src : remove `len` elements at `start`
   and insert the `srcn` elements of `src` in their place, shifting the tail.
   Reuses push so capacity growth + GC byte accounting stay in one place. src
   is snapshotted first so splicing an array into itself (`a[1,1]=a`) is safe.
   A start beyond the end would need a nil gap, which a typed array cannot
   hold, so it raises (the poly-array splice fills nil instead). */
void sp_IntArray_splice(sp_IntArray*a,sp_int start,sp_int len,const sp_int*src,sp_int srcn){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}
  /* The equal-length overwrite below allocates nothing, so it needs no root --
     and it is the hot shape (optcarrot's per-tile `@bg_pixels[x, 8] = row`).
     Root only on the way into the general path, which does allocate. */
  sp_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  if(s>alen){sp_raise_cls("RuntimeError",sp_sprintf("index %lld out of range for typed-array splice (would require nil fill)",(long long)s));return;}
  if(s+len>alen)len=alen-s;
  /* Equal-length replacement is a pure overwrite: no length change, no
     capacity growth, no tail shift. memmove, since src may alias a's own
     buffer (self-splice). This is the hot shape (optcarrot's per-tile
     `@bg_pixels[x, 8] = <8-elem row>`); the general path below allocates. */
  if(len==srcn){if(srcn>0)memmove(a->data+a->start+s,src,sizeof(sp_int)*(size_t)srcn);return;}
  SP_GC_ROOT(a);
  sp_int*sb=NULL;
  if(srcn>0){sb=(sp_int*)sp_pl_alloc(sizeof(sp_int)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,src,sizeof(sp_int)*(size_t)srcn);}
  sp_int tail_from=s+len,tail_n=alen-tail_from;
  sp_int*tb=NULL;
  if(tail_n>0){tb=(sp_int*)sp_pl_alloc(sizeof(sp_int)*(size_t)tail_n);if(!tb){sp_pl_free(sb);sp_oom_die();}memcpy(tb,a->data+a->start+tail_from,sizeof(sp_int)*(size_t)tail_n);}
  a->len=s;
  for(sp_int i=0;i<srcn;i++)sp_IntArray_push(a,sb[i]);
  for(sp_int i=0;i<tail_n;i++)sp_IntArray_push(a,tb[i]);
  sp_pl_free(sb);sp_pl_free(tb);
}
void sp_FloatArray_splice(sp_FloatArray*a,sp_int start,sp_int len,const sp_float*src,sp_int srcn){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}
  sp_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  if(s>alen){sp_raise_cls("RuntimeError",sp_sprintf("index %lld out of range for typed-array splice (would require nil fill)",(long long)s));return;}
  if(s+len>alen)len=alen-s;
  if(len==srcn){if(srcn>0)memmove(a->data+s,src,sizeof(sp_float)*(size_t)srcn);return;}  /* see the int form */
  SP_GC_ROOT(a);
  sp_float*sb=NULL;
  if(srcn>0){sb=(sp_float*)sp_pl_alloc(sizeof(sp_float)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,src,sizeof(sp_float)*(size_t)srcn);}
  sp_int tail_from=s+len,tail_n=alen-tail_from;
  sp_float*tb=NULL;
  if(tail_n>0){tb=(sp_float*)sp_pl_alloc(sizeof(sp_float)*(size_t)tail_n);if(!tb){sp_pl_free(sb);sp_oom_die();}memcpy(tb,a->data+tail_from,sizeof(sp_float)*(size_t)tail_n);}
  a->len=s;
  for(sp_int i=0;i<srcn;i++)sp_FloatArray_push(a,sb[i]);
  for(sp_int i=0;i<tail_n;i++)sp_FloatArray_push(a,tb[i]);
  sp_pl_free(sb);sp_pl_free(tb);
}
void sp_StrArray_splice(sp_StrArray*a,sp_int start,sp_int len,const char*const*src,sp_int srcn){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}
  sp_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  if(s>alen){sp_raise_cls("RuntimeError",sp_sprintf("index %lld out of range for typed-array splice (would require nil fill)",(long long)s));return;}
  if(s+len>alen)len=alen-s;
  if(len==srcn){if(srcn>0)memmove(a->data+s,src,sizeof(const char*)*(size_t)srcn);return;}  /* see the int form */
  SP_GC_ROOT(a);
  const char**sb=NULL;
  if(srcn>0){sb=(const char**)sp_pl_alloc(sizeof(const char*)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,src,sizeof(const char*)*(size_t)srcn);}
  /* Snapshot the tail into a *rooted* holder before truncating a->len: once
     a->len shrinks, a no longer scans the tail, so a raw buffer would leave the
     tail's GC strings unrooted across the pushes below (which can collect). The
     root sits at function scope -- not inside the guard -- so it stays live for
     the whole splice. */
  sp_int tail_from=s+len,tail_n=alen-tail_from;
  sp_StrArray*tb=NULL;SP_GC_ROOT(tb);
  if(tail_n>0){tb=sp_StrArray_new();for(sp_int i=0;i<tail_n;i++)sp_StrArray_push(tb,a->data[tail_from+i]);}
  a->len=s;
  for(sp_int i=0;i<srcn;i++)sp_StrArray_push(a,sb[i]);
  if(tb)for(sp_int i=0;i<tb->len;i++)sp_StrArray_push(a,tb->data[i]);
  sp_pl_free(sb);
}
/* poly-array splice: like the typed forms but elements are boxed, so a nil gap
   (start past the end) is filled with nil rather than raising. Ruby's `a[s,l]=`
   splices the RHS's elements when it is an Array, else inserts it as one
   element -- a runtime fact decided here from src's class id. */
void sp_PolyArray_splice(sp_PolyArray*a,sp_int start,sp_int len,sp_RbVal src){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_POLY_ARRAY);return;}
  SP_GC_ROOT(a);
  /* src rooted too: the pushes below can trigger a collection while some of
     the snapshotted elements are reachable only through src. */
  SP_GC_ROOT_RBVAL(src);
  sp_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  /* Equal-length replacement inside the array is a pure overwrite, as in the
     typed splices above: no snapshot buffer, no tail copy, no pushes. It was
     ~190x the typed arrays' cost on `colors[x, 8] = chunk` (#4841). One
     barrier covers the stores, as sp_PolyArray_set takes one; a poly source
     may be this very array, hence memmove. */
  if(len>0&&s+len<=alen&&src.tag==SP_TAG_OBJ&&src.v.p){
    sp_RbVal*dst=a->data+s;
    switch(src.cls_id){
      case SP_BUILTIN_POLY_ARRAY:{sp_PolyArray*x=(sp_PolyArray*)src.v.p;if(x->len==len){sp_gc_wb((void*)a);memmove(dst,x->data,sizeof(sp_RbVal)*(size_t)len);return;}break;}
      case SP_BUILTIN_INT_ARRAY:{sp_IntArray*x=(sp_IntArray*)src.v.p;if(x->len==len){sp_gc_wb((void*)a);for(sp_int i=0;i<len;i++)dst[i]=sp_box_int(x->data[x->start+i]);return;}break;}
      case SP_BUILTIN_FLT_ARRAY:{sp_FloatArray*x=(sp_FloatArray*)src.v.p;if(x->len==len){sp_gc_wb((void*)a);for(sp_int i=0;i<len;i++)dst[i]=sp_box_float(x->data[i]);return;}break;}
      case SP_BUILTIN_STR_ARRAY:{sp_StrArray*x=(sp_StrArray*)src.v.p;if(x->len==len){sp_gc_wb((void*)a);for(sp_int i=0;i<len;i++)dst[i]=sp_box_str(x->data[i]);return;}break;}
      default:break;
    }
  }
  /* snapshot the source elements as boxed values. src's class id decides
     array-vs-single-element (Ruby splices an Array RHS, inserts anything
     else). A user object with to_ary is coerced at COMPILE time when its
     static type is known; a to_ary object reaching here as a runtime poly
     value still inserts as one element -- closing that would need a
     codegen-installed dispatch hook (the sp_obj_cmp_hook pattern). */
  int src_is_array=src.tag==SP_TAG_OBJ&&(src.cls_id==SP_BUILTIN_INT_ARRAY||src.cls_id==SP_BUILTIN_FLT_ARRAY||src.cls_id==SP_BUILTIN_STR_ARRAY||src.cls_id==SP_BUILTIN_POLY_ARRAY||src.cls_id==SP_BUILTIN_PTR_ARRAY);
  sp_int srcn=0;sp_RbVal*sb=NULL;
  if(src_is_array){
    void*p=src.v.p;
    switch(src.cls_id){
      case SP_BUILTIN_INT_ARRAY:{sp_IntArray*x=(sp_IntArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)sp_pl_alloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(sp_int i=0;i<srcn;i++)sb[i]=sp_box_int(x->data[x->start+i]);}break;}
      case SP_BUILTIN_FLT_ARRAY:{sp_FloatArray*x=(sp_FloatArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)sp_pl_alloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(sp_int i=0;i<srcn;i++)sb[i]=sp_box_float(x->data[i]);}break;}
      case SP_BUILTIN_STR_ARRAY:{sp_StrArray*x=(sp_StrArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)sp_pl_alloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(sp_int i=0;i<srcn;i++)sb[i]=sp_box_str(x->data[i]);}break;}
      case SP_BUILTIN_POLY_ARRAY:{sp_PolyArray*x=(sp_PolyArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)sp_pl_alloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,x->data,sizeof(sp_RbVal)*(size_t)srcn);}break;}
      case SP_BUILTIN_PTR_ARRAY:{sp_PtrArray*x=(sp_PtrArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)sp_pl_alloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(sp_int i=0;i<srcn;i++)sb[i]=sp_PtrArray_elem_box(x,x->data[i]);}break;}   /* rows or objects (#4486) */
      default:break;
    }
  }
  else{
    srcn=1;sb=(sp_RbVal*)sp_pl_alloc(sizeof(sp_RbVal));if(!sb)sp_oom_die();sb[0]=src;
  }
  /* clamp/gap: when start is past the end, the [s+len) tail is empty and the
     [alen,s) gap fills with nil */
  sp_int gap=0,tail_from,tail_n;
  if(s<=alen){if(s+len>alen)len=alen-s;tail_from=s+len;tail_n=alen-tail_from;}
  else{gap=s-alen;tail_from=alen;tail_n=0;}
  /* Snapshot the tail into a *rooted* holder before truncating a->len: the tail
     elements belong to a and, once a->len shrinks, are reachable through neither
     a nor src, so a raw buffer would leave them unrooted across the pushes below
     (which can collect). The source snapshot sb needs no such holder -- its
     elements stay reachable through the already-rooted src. */
  sp_PolyArray*tb=NULL;SP_GC_ROOT(tb);
  if(tail_n>0){tb=sp_PolyArray_new();for(sp_int i=0;i<tail_n;i++)sp_PolyArray_push(tb,a->data[tail_from+i]);}
  a->len=(s<=alen)?s:alen;
  for(sp_int i=0;i<gap;i++)sp_PolyArray_push(a,sp_box_nil());
  for(sp_int i=0;i<srcn;i++)sp_PolyArray_push(a,sb[i]);
  if(tb)for(sp_int i=0;i<tb->len;i++)sp_PolyArray_push(a,tb->data[i]);
  sp_pl_free(sb);
}
void sp_IntArray_reverse_bang(sp_IntArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}for(sp_int i=0,j=a->len-1;i<j;i++,j--){sp_int t=a->data[a->start+i];a->data[a->start+i]=a->data[a->start+j];a->data[a->start+j]=t;}}
/* Array#rotate!: move the first n elements to the end. Two block moves --
   save the smaller side, slide the rest, write the saved side back -- rather
   than the triple-reverse's ~1.5n element swaps. The saved side is at most
   half the array, so a small stack buffer covers the sizes that matter (a
   16-element PPU pixel row is rotated once per tile fetch). */
void sp_IntArray_rotate_bang(sp_IntArray*a,sp_int n){SP_GC_ROOT(a);
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}
  if(a->len<=0)return;
  n=((n%a->len)+a->len)%a->len;
  if(n==0)return;
  /* Rotating left by n is "the first n elements move to the end", and the
     window already carries its own start offset (the same one push_grow slides
     home). So it is one copy of n words and an index bump, where the in-place
     form below costs a copy of n, a move of len-n, and a copy back -- three
     times the traffic for optcarrot's 16-word pixel window rotated by eight,
     twice per tile. Headroom past the window is what pays for it: grow once to
     hold a few rotations, and slide home when it runs out, which amortizes to
     well under the old cost. */
  if(a->start+a->len+n>a->cap && a->len+n<=a->cap && a->start>0){
    memmove(a->data,a->data+a->start,sizeof(sp_int)*(size_t)a->len);
    a->start=0;
  }
  /* Only grow for a window small enough that the headroom is cheap and the
     rotation is plausibly repeated (a pixel window, a ring of recent values).
     A one-shot rotate of a large array would pay a doubled buffer for a copy
     it never repeats, so that keeps the in-place form. */
  if(a->start+a->len+n>a->cap && a->len+n>a->cap && a->len<=256){
    sp_int nc=a->len*2+n;
    sp_int*nd=(sp_int*)sp_pl_alloc(sizeof(sp_int)*(size_t)nc);
    if(nd){
      memcpy(nd,a->data+a->start,sizeof(sp_int)*(size_t)a->len);
      sp_pl_free(a->data); a->data=nd; a->start=0; a->cap=nc;
    }
  }
  if(a->start+a->len+n<=a->cap){
    memcpy(a->data+a->start+a->len,a->data+a->start,sizeof(sp_int)*(size_t)n);
    a->start+=n;
    return;
  }
  sp_int*d=a->data+a->start;
  sp_int rest=a->len-n;
  sp_int keep=n<rest?n:rest;          /* the side we buffer */
  sp_int stackbuf[32];
  sp_int*t=keep<=32?stackbuf:(sp_int*)sp_pl_alloc(sizeof(sp_int)*(size_t)keep);
  if(!t)sp_oom_die();
  if(n<=rest){                          /* buffer the head, slide the tail down */
    memcpy(t,d,sizeof(sp_int)*(size_t)n);
    memmove(d,d+n,sizeof(sp_int)*(size_t)rest);
    memcpy(d+rest,t,sizeof(sp_int)*(size_t)n);
  }
  else{                                 /* buffer the tail, slide the head up */
    memcpy(t,d+n,sizeof(sp_int)*(size_t)rest);
    memmove(d+rest,d,sizeof(sp_int)*(size_t)n);
    memcpy(d,t,sizeof(sp_int)*(size_t)rest);
  }
  if(t!=stackbuf)sp_pl_free(t);
}
static int _sp_int_cmp(const void*a,const void*b){sp_int va=*(const sp_int*)a,vb=*(const sp_int*)b;return(va>vb)-(va<vb);}
sp_IntArray*sp_IntArray_sort(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*b=sp_IntArray_dup(a);qsort(b->data+b->start,b->len,sizeof(sp_int),_sp_int_cmp);return b;}
void sp_IntArray_sort_bang(sp_IntArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}qsort(a->data+a->start,a->len,sizeof(sp_int),_sp_int_cmp);}
void sp_IntArray_uniq_bang(sp_IntArray*a){SP_GC_ROOT(a);if(!a||a->frozen){if(a&&a->frozen)sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}for(sp_int i=0;i<a->len;){int dup=0;for(sp_int j=0;j<i;j++){if(a->data[a->start+j]==a->data[a->start+i]){dup=1;break;}}if(dup){for(sp_int k2=i;k2<a->len-1;k2++)a->data[a->start+k2]=a->data[a->start+k2+1];a->len--;}
else i++;}}
void sp_IntArray_shuffle_bang(sp_IntArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}for(sp_int i=a->len-1;i>0;i--){sp_int j=sp_krand_below(i+1);sp_int t=a->data[a->start+i];a->data[a->start+i]=a->data[a->start+j];a->data[a->start+j]=t;}}
sp_IntArray*sp_IntArray_shuffle(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*b=sp_IntArray_dup(a);sp_IntArray_shuffle_bang(b);return b;}
/* Array#sample. CRuby returns nil for `[].sample`; in spinel's typed-array
   slot nil collapses to 0. sp_krand_below guards the len==0 draw itself.
   Issue #536. */
sp_int sp_IntArray_sample(sp_IntArray*a){SP_GC_ROOT(a);if(a->len<=0)return 0;return a->data[a->start+sp_krand_below(a->len)];}
/* Issue #745/#832: empty min/max return SP_INT_NIL (caller treats as
   int?); without the guard, the first read is uninitialized memory. */
sp_int sp_IntArray_min(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;sp_int m=a->data[a->start];for(sp_int i=1;i<a->len;i++)if(a->data[a->start+i]<m)m=a->data[a->start+i];return m;}
sp_int sp_IntArray_max(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;sp_int m=a->data[a->start];for(sp_int i=1;i<a->len;i++)if(a->data[a->start+i]>m)m=a->data[a->start+i];return m;}
sp_int sp_IntArray_sum(sp_IntArray*a,sp_int init){sp_int s=init;for(sp_int i=0;i<a->len;i++)s+=a->data[a->start+i];return s;}
sp_bool sp_IntArray_include(sp_IntArray*a,sp_int v){if(!a)return FALSE;for(sp_int i=0;i<a->len;i++)if(a->data[a->start+i]==v)return TRUE;return FALSE;}
sp_int sp_IntArray_index(sp_IntArray*a,sp_int v){for(sp_int i=0;i<a->len;i++)if(a->data[a->start+i]==v)return i;return -1;}
sp_int sp_IntArray_rindex(sp_IntArray*a,sp_int v){for(sp_int i=a->len-1;i>=0;i--)if(a->data[a->start+i]==v)return i;return -1;}
sp_int sp_IntArray_delete_at(sp_IntArray*a,sp_int i){SP_GC_ROOT(a);if(a&&a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return SP_INT_NIL;}if(i<0)i+=a->len;if(i<0||i>=a->len)return SP_INT_NIL;sp_int v=a->data[a->start+i];for(sp_int j=i;j<a->len-1;j++)a->data[a->start+j]=a->data[a->start+j+1];a->len--;return v;}
sp_int sp_IntArray_delete(sp_IntArray*a,sp_int v){SP_GC_ROOT(a);if(a&&a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return 0;}sp_int w=0;for(sp_int i=0;i<a->len;i++){if(a->data[a->start+i]!=v){a->data[a->start+w]=a->data[a->start+i];w++;}}sp_int d=a->len-w;a->len=w;return d>0?v:SP_INT_NIL;}  /* CRuby: nil when absent */
/* Issue #788: clamp i so a very-negative index doesn't underflow past
   a->start and write into the array's GC header. */
void sp_IntArray_insert(sp_IntArray*a,sp_int i,sp_int v){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}sp_int orig=i;if(i<0)i+=a->len+1;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)(-(a->len+1))));while(i>a->len)sp_IntArray_push(a,SP_INT_NIL);/* CRuby pads with nils past the end */sp_IntArray_push(a,0);for(sp_int j=a->len-1;j>i;j--)a->data[a->start+j]=a->data[a->start+j-1];a->data[a->start+i]=v;}
sp_IntArray*sp_IntArray_uniq(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*b=sp_IntArray_new();for(sp_int i=0;i<a->len;i++){int found=0;for(sp_int j=0;j<b->len;j++){if(b->data[b->start+j]==a->data[a->start+i]){found=1;break;}}if(!found)sp_IntArray_push(b,a->data[a->start+i]);}return b;}
sp_IntArray*sp_IntArray_intersect(sp_IntArray*a,sp_IntArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_IntArray*r=sp_IntArray_new();if(!a||!b)return r;for(sp_int i=0;i<a->len;i++){sp_int v=a->data[a->start+i];if(sp_IntArray_include(b,v)&&!sp_IntArray_include(r,v))sp_IntArray_push(r,v);}return r;}
sp_bool sp_IntArray_intersect_p(sp_IntArray*a,sp_IntArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);if(!a||!b)return 0;for(sp_int i=0;i<a->len;i++)if(sp_IntArray_include(b,a->data[a->start+i]))return 1;return 0;}
sp_IntArray*sp_IntArray_union(sp_IntArray*a,sp_IntArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_IntArray*r=sp_IntArray_new();if(a)for(sp_int i=0;i<a->len;i++){sp_int v=a->data[a->start+i];if(!sp_IntArray_include(r,v))sp_IntArray_push(r,v);}if(b){for(sp_int i=0;i<b->len;i++){sp_int v=b->data[b->start+i];if(!sp_IntArray_include(r,v))sp_IntArray_push(r,v);}}return r;}
/* Array#- / Array#difference: keep every LHS element NOT in RHS,
   preserving the LHS's duplicates. `[1,1,2,3] - [3]` is `[1,1,2]`. */
sp_IntArray*sp_IntArray_difference(sp_IntArray*a,sp_IntArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_IntArray*r=sp_IntArray_new();if(!a)return r;for(sp_int i=0;i<a->len;i++){sp_int v=a->data[a->start+i];if(!sp_IntArray_include(b,v))sp_IntArray_push(r,v);}return r;}
void sp_IntArray_unshift(sp_IntArray*a,sp_int v){SP_GC_ROOT(a);if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}if(a->start>0){a->start--;a->data[a->start]=v;a->len++;}
else{sp_int e=a->len+1;if(e>a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*a->cap);h->size-=sizeof(sp_int)*a->cap;a->cap=(((((a->cap*2)))))+1;a->data=(sp_int*)sp_pl_realloc(a->data,sizeof(sp_int)*a->cap);h->size+=sizeof(sp_int)*a->cap;sp_gc_bytes_add(sizeof(sp_int)*a->cap);}memmove(a->data+1,a->data,sizeof(sp_int)*a->len);a->data[0]=v;a->len++;}}
const char*sp_IntArray_join(sp_IntArray*a,const char*sep){if(!sep)sep="";size_t sl=sp_str_byte_len(sep),cap=256;char*buf=(char*)sp_pl_alloc(cap);size_t len=0;for(sp_int i=0;i<a->len;i++){if(i>0){if(len+sl>=cap){cap*=2;buf=(char*)sp_pl_realloc(buf,cap);}memcpy(buf+len,sep,sl);len+=sl;}char tmp[32];int n=snprintf(tmp,32,"%lld",(long long)a->data[a->start+i]);if(len+n>=cap){cap*=2;buf=(char*)sp_pl_realloc(buf,cap);}memcpy(buf+len,tmp,n);len+=n;}buf[len]=0;char*r=sp_str_alloc(len);memcpy(r,buf,len);sp_str_set_len(r,len);sp_pl_free(buf);return r;}
sp_bool sp_IntArray_eq(sp_IntArray*a,sp_IntArray*b){if(!a||!b)return a==b;if(a->len!=b->len)return FALSE;for(sp_int i=0;i<a->len;i++)if(a->data[a->start+i]!=b->data[b->start+i])return FALSE;return TRUE;}
/* Array#<=> for IntArray. Lexicographic: per-element compare, shorter
   array sorts before longer if all common elements match
   (`[1,2] <=> [1,2,3] == -1`). NULL recv compares equal to NULL, lower
   than any non-NULL. */
sp_int sp_IntArray_cmp(sp_IntArray*a,sp_IntArray*b){if(!a||!b)return a==b?0:(a?1:-1);sp_int n=a->len<b->len?a->len:b->len;for(sp_int i=0;i<n;i++){sp_int av=a->data[a->start+i],bv=b->data[b->start+i];if(av<bv)return -1;if(av>bv)return 1;}if(a->len<b->len)return -1;if(a->len>b->len)return 1;return 0;}

/* ============================ sp_FloatArray ============================ */
void sp_FloatArray_unshift(sp_FloatArray*a,sp_float v){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}sp_FloatArray_push(a,0.0);if(a->len>1)memmove(&a->data[1],&a->data[0],(size_t)(a->len-1)*sizeof(sp_float));a->data[0]=v;}
/* (beg..end).step(step) / Float#step materialised as a FloatArray, following
   CRuby's ruby_float_step: an epsilon-corrected element count (so float drift
   never drops or adds a value) with each value computed as beg + i*step rather
   than by repeated addition. Honours range exclusivity; step==0 raises
   ArgumentError like CRuby; the final value is clamped to end on overshoot. */
sp_FloatArray*sp_FloatArray_from_step(sp_float beg,sp_float end,sp_float step,sp_int excl){
  if(step==0.0)sp_raise_cls("ArgumentError","step can't be 0");
  /* CRuby rejects a NaN range bound at construction with this message; a NaN
     step is just as degenerate. Guard before any arithmetic so the count never
     becomes NaN (whose cast to sp_int is undefined behaviour). */
  if(isnan(beg)||isnan(end)||isnan(step))sp_raise_cls("ArgumentError","bad value for range");
  sp_FloatArray*a=sp_FloatArray_new();
  if(isinf(step)){if(step>0?beg<=end:beg>=end)sp_FloatArray_push(a,beg);return a;}
  sp_float n=(end-beg)/step;
  sp_float err=(fabs(beg)+fabs(end)+fabs(end-beg))/fabs(step)*DBL_EPSILON;
  if(err>0.5)err=0.5;
  if(excl){
    if(n<=0)return a;
    n=(n<1)?0:floor(n-err);
    sp_float d=((n+1)*step)+beg;
    if(beg<end){if(d<end)n++;}
    else if(beg>end){if(d>end)n++;}
  }
  else{
    if(n<0)return a;
    n=floor(n+err);
    sp_float d=((n+1)*step)+beg;
    if(beg<end){if(d<=end)n++;}
    else if(beg>end){if(d>=end)n++;}
  }
  /* An infinite end (or an enormous span) drives n to infinity or past what can
     be materialised; CRuby loops forever there. Raise rather than cast a
     non-finite/huge double to sp_int (undefined behaviour) and exhaust memory.
     The 1<<30 cap matches sp_IntArray_from_range's sanity bound. */
  if(isinf(n)||n>=(sp_float)(1LL<<30))sp_raise_cls("RangeError","range too large to materialize");
  sp_int count=(sp_int)n+1;
  for(sp_int i=0;i<count;i++){
    sp_float d=((sp_float)i*step)+beg;
    if(step>=0?end<d:d<end)d=end;
    sp_FloatArray_push(a,d);
  }
  return a;
}
sp_float sp_FloatArray_min(sp_FloatArray*a){if(!a||a->len==0)return sp_float_nil();  /* CRuby: nil on empty (#4288) */ sp_float m=a->data[0];for(sp_int i=1;i<a->len;i++)if(a->data[i]<m)m=a->data[i];return m;}
sp_float sp_FloatArray_max(sp_FloatArray*a){if(!a||a->len==0)return sp_float_nil();  /* CRuby: nil on empty (#4288) */ sp_float m=a->data[0];for(sp_int i=1;i<a->len;i++)if(a->data[i]>m)m=a->data[i];return m;}
/* Array#sum for floats: Kahan-Babuska-Neumaier compensated summation,
   matching CRuby (float addition isn't associative, so a naive fold of
   [0.1,0.2,0.3] yields 0.6000000000000001 instead of 0.6). The step, with
   CRuby's NaN/Infinity arms, is sp_float_sum_step in sp_array.h -- shared with
   the boxed fold so the two cannot drift. */
sp_float sp_FloatArray_sum(sp_FloatArray*a,sp_float init){sp_float s=init,c=0.0;for(sp_int i=0;i<a->len;i++)sp_float_sum_step(&s,&c,a->data[i]);return s+c;}
/* ...and the same sum from a FLOAT seed, which CRuby does not compensate: it
   reaches the compensated loop only out of the exact phase, and a seed that is
   already a Float never has one. `[0.1, 0.2, 0.3].sum(0.0)` is therefore
   0.6000000000000001 where `.sum(0)` and `.sum` are 0.6. The boxed fold in
   spinel_rt.h draws the same line; these two must not disagree. */
sp_float sp_FloatArray_sum_plain(sp_FloatArray*a,sp_float init){sp_float s=init;for(sp_int i=0;i<a->len;i++)s+=a->data[i];return s;}
void sp_FloatArray_replace(sp_FloatArray*dst,sp_FloatArray*src){if(dst==src)return;if(dst->frozen){sp_raise_frozen_array_at(dst,SP_BUILTIN_FLT_ARRAY);return;}dst->len=0;if(src->len>dst->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)dst-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_float)*dst->cap);h->size-=sizeof(sp_float)*dst->cap;void*nd=sp_pl_realloc(dst->data,sizeof(sp_float)*src->len);if(!nd){perror("realloc");exit(1);}dst->data=(sp_float*)nd;dst->cap=src->len;h->size+=sizeof(sp_float)*dst->cap;sp_gc_bytes_add(sizeof(sp_float)*dst->cap);}memcpy(dst->data,src->data,sizeof(sp_float)*src->len);dst->len=src->len;}
/* a[start, len] / a[start..end] for FloatArray. Same negative-start and
   length-clamping semantics as sp_IntArray_slice. */
sp_FloatArray*sp_FloatArray_slice(sp_FloatArray*a,sp_int start,sp_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_FloatArray*b=sp_FloatArray_new();if(start>=a->len||len<=0)return b;if(len>a->len-start)len=a->len-start;if(len>b->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)b-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_float)*b->cap);h->size-=sizeof(sp_float)*b->cap;b->cap=len;b->data=(sp_float*)sp_pl_realloc(b->data,sizeof(sp_float)*b->cap);h->size+=sizeof(sp_float)*b->cap;sp_gc_bytes_add(sizeof(sp_float)*b->cap);}memcpy(b->data,a->data+start,sizeof(sp_float)*len);b->len=len;return b;}
sp_FloatArray*sp_FloatArray_slice_range(sp_FloatArray*a,sp_int start,sp_int end_,sp_int excl){SP_GC_ROOT(a);if(end_<0)end_+=a->len;if(start<0)start+=a->len;if(start<0||start>a->len)return NULL;/* a start outside [-len, len] is nil (#4524); start == len is [] */sp_int n=end_-start+(excl?0:1);if(n<0)n=0;return sp_FloatArray_slice(a,start,n);}
void sp_FloatArray_reverse_bang(sp_FloatArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}for(sp_int i=0,j=a->len-1;i<j;i++,j--){sp_float t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
void sp_FloatArray_rotate_bang(sp_FloatArray*a,sp_int n){SP_GC_ROOT(a);
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}
  if(a->len<=0)return;
  n=((n%a->len)+a->len)%a->len;
  if(n==0)return;
  sp_float*d=a->data;
  sp_int rest=a->len-n;
  sp_int keep=n<rest?n:rest;
  sp_float stackbuf[32];
  sp_float*t=keep<=32?stackbuf:(sp_float*)sp_pl_alloc(sizeof(sp_float)*(size_t)keep);
  if(!t)sp_oom_die();
  if(n<=rest){
    memcpy(t,d,sizeof(sp_float)*(size_t)n);
    memmove(d,d+n,sizeof(sp_float)*(size_t)rest);
    memcpy(d+rest,t,sizeof(sp_float)*(size_t)n);
  }
  else{
    memcpy(t,d+n,sizeof(sp_float)*(size_t)rest);
    memmove(d+rest,d,sizeof(sp_float)*(size_t)n);
    memcpy(d,t,sizeof(sp_float)*(size_t)rest);
  }
  if(t!=stackbuf)sp_pl_free(t);
}
static int _sp_float_cmp(const void*a,const void*b){sp_float va=*(const sp_float*)a,vb=*(const sp_float*)b;return(va>vb)-(va<vb);}
void sp_FloatArray_sort_bang(sp_FloatArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}qsort(a->data,a->len,sizeof(sp_float),_sp_float_cmp);}
void sp_FloatArray_shuffle_bang(sp_FloatArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}for(sp_int i=a->len-1;i>0;i--){sp_int j=sp_krand_below(i+1);sp_float t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
sp_FloatArray*sp_FloatArray_dup(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*b=sp_FloatArray_new();sp_FloatArray_replace(b,a);return b;}
sp_FloatArray*sp_FloatArray_sort(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*b=sp_FloatArray_dup(a);sp_FloatArray_sort_bang(b);return b;}
sp_FloatArray*sp_FloatArray_shuffle(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*r=sp_FloatArray_new();sp_FloatArray_replace(r,a);sp_FloatArray_shuffle_bang(r);return r;}
sp_float sp_FloatArray_sample(sp_FloatArray*a){SP_GC_ROOT(a);if(a->len<=0)return 0.0;return a->data[sp_krand_below(a->len)];}
/* IEEE 754 == on sp_float: NaN never matches; +0.0 == -0.0 (diverges from Float#eql?). */
/* a NaN is not == to itself, but CRuby's identity fallback still finds the
   very same NaN in a container (#3650) */
sp_bool sp_FloatArray_include(sp_FloatArray*a,sp_float v){if(!a)return FALSE;for(sp_int i=0;i<a->len;i++){if(a->data[i]==v)return TRUE;if(v!=v&&a->data[i]!=a->data[i]&&memcmp(&a->data[i],&v,sizeof v)==0)return TRUE;}return FALSE;}
/* index/rindex/delete match the way sp_FloatArray_include just above does:
   == first, then the bit-pattern fallback so the Float::NAN constant finds
   itself (a computed NaN with the same bits over-matches identically).
   delete answers the deleted ELEMENT, last match, as CRuby does -- -0.0
   shows the difference -- and nil (an e never reassigned) when absent. */
sp_int sp_FloatArray_index(sp_FloatArray*a,sp_float v){if(!a)return -1;for(sp_int i=0;i<a->len;i++){sp_float x=a->data[i];if(x==v||(v!=v&&x!=x&&memcmp(&x,&v,sizeof v)==0))return i;}return -1;}
sp_int sp_FloatArray_rindex(sp_FloatArray*a,sp_float v){if(!a)return -1;for(sp_int i=a->len-1;i>=0;i--){sp_float x=a->data[i];if(x==v||(v!=v&&x!=x&&memcmp(&x,&v,sizeof v)==0))return i;}return -1;}
sp_float sp_FloatArray_delete(sp_FloatArray*a,sp_float v){SP_GC_ROOT(a);if(!a)return sp_float_nil();if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return sp_float_nil();}sp_int w=0;sp_float e=sp_float_nil();for(sp_int i=0;i<a->len;i++){sp_float x=a->data[i];if(x==v||(v!=v&&x!=x&&memcmp(&x,&v,sizeof v)==0))e=x;else{a->data[w]=x;w++;}}a->len=w;return e;}
sp_FloatArray*sp_FloatArray_intersect(sp_FloatArray*a,sp_FloatArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_FloatArray*r=sp_FloatArray_new();if(!a||!b)return r;for(sp_int i=0;i<a->len;i++){sp_float v=a->data[i];if(sp_FloatArray_include(b,v)&&!sp_FloatArray_include(r,v))sp_FloatArray_push(r,v);}return r;}
sp_bool sp_FloatArray_intersect_p(sp_FloatArray*a,sp_FloatArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);if(!a||!b)return 0;for(sp_int i=0;i<a->len;i++)if(sp_FloatArray_include(b,a->data[i]))return 1;return 0;}
sp_FloatArray*sp_FloatArray_union(sp_FloatArray*a,sp_FloatArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_FloatArray*r=sp_FloatArray_new();if(a)for(sp_int i=0;i<a->len;i++){sp_float v=a->data[i];if(!sp_FloatArray_include(r,v))sp_FloatArray_push(r,v);}if(b){for(sp_int i=0;i<b->len;i++){sp_float v=b->data[i];if(!sp_FloatArray_include(r,v))sp_FloatArray_push(r,v);}}return r;}
sp_FloatArray*sp_FloatArray_difference(sp_FloatArray*a,sp_FloatArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_FloatArray*r=sp_FloatArray_new();for(sp_int i=0;i<a->len;i++){sp_float v=a->data[i];if(!sp_FloatArray_include(b,v))sp_FloatArray_push(r,v);}return r;}
sp_FloatArray*sp_FloatArray_uniq(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*b=sp_FloatArray_new();if(!a)return b;for(sp_int i=0;i<a->len;i++){sp_float v=sp_FloatArray_get(a,i);if(!sp_FloatArray_include(b,v))sp_FloatArray_push(b,v);}return b;}

/* ============================= sp_PtrArray ============================ */
/* `Array#delete_at(i)` -- remove and return the element at index i.
   Negative indices count from the end; NULL when out of range. */
void*sp_PtrArray_delete_at(sp_PtrArray*a,sp_int i){ sp_gc_wb((void*)a);if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;void*v=a->data[i];for(sp_int j=i;j<a->len-1;j++)a->data[j]=a->data[j+1];a->len--;return v;}
void sp_PtrArray_reverse_bang(sp_PtrArray*a){ sp_gc_wb((void*)a);for(sp_int i=0,j=a->len-1;i<j;i++,j--){void*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
void sp_PtrArray_rotate_bang(sp_PtrArray*a,sp_int n){
  if(!a)return;
  if(a->len<=0)return;
  n=((n%a->len)+a->len)%a->len;
  if(n==0)return;
  void**d=a->data;
  sp_int rest=a->len-n;
  sp_int keep=n<rest?n:rest;
  void* stackbuf[32];
  void**t=keep<=32?stackbuf:(void**)sp_pl_alloc(sizeof(void*)*(size_t)keep);
  if(!t)sp_oom_die();
  if(n<=rest){
    memcpy(t,d,sizeof(void*)*(size_t)n);
    memmove(d,d+n,sizeof(void*)*(size_t)rest);
    memcpy(d+rest,t,sizeof(void*)*(size_t)n);
  }
  else{
    memcpy(t,d+n,sizeof(void*)*(size_t)rest);
    memmove(d+rest,d,sizeof(void*)*(size_t)n);
    memcpy(d,t,sizeof(void*)*(size_t)rest);
  }
  if(t!=stackbuf)sp_pl_free(t);
}
sp_PtrArray*sp_PtrArray_dup(sp_PtrArray*a){SP_GC_ROOT(a);sp_PtrArray*b=sp_PtrArray_new_scan(a->scan_elem);for(sp_int i=0;i<a->len;i++)sp_PtrArray_push(b,a->data[i]);return b;}
sp_PtrArray*sp_PtrArray_slice(sp_PtrArray*a,sp_int start,sp_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_PtrArray*b=sp_PtrArray_new_scan(a->scan_elem);if(start>=a->len||len<=0)return b;if(len>a->len-start)len=a->len-start;for(sp_int i=0;i<len;i++)sp_PtrArray_push(b,a->data[start+i]);return b;}
void sp_PtrArray_shuffle_bang(sp_PtrArray*a){SP_GC_ROOT(a); sp_gc_wb((void*)a);for(sp_int i=a->len-1;i>0;i--){sp_int j=sp_krand_below(i+1);void*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
sp_PtrArray*sp_PtrArray_shuffle(sp_PtrArray*a){SP_GC_ROOT(a);sp_PtrArray*b=sp_PtrArray_dup(a);sp_PtrArray_shuffle_bang(b);return b;}
void *sp_PtrArray_sample(sp_PtrArray*a){SP_GC_ROOT(a);if(a->len<=0)return NULL;return a->data[sp_krand_below(a->len)];}

/* ============================= sp_StrArray ============================ */
void sp_StrArray_replace(sp_StrArray*dst,sp_StrArray*src){if(dst==src)return;if(dst->frozen){sp_raise_frozen_array_at(dst,SP_BUILTIN_STR_ARRAY);return;} sp_gc_wb((void*)dst);dst->len=0;if(src->len>dst->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)dst-sizeof(sp_gc_hdr));void*nd;if(dst->data==dst->inline_data){nd=sp_pl_alloc(sizeof(const char*)*src->len);if(!nd){perror("malloc");exit(1);}}
else{sp_gc_bytes_sub(sizeof(const char*)*dst->cap);h->size-=sizeof(const char*)*dst->cap;nd=sp_pl_realloc(dst->data,sizeof(const char*)*src->len);if(!nd){perror("realloc");exit(1);}}dst->data=(const char**)nd;dst->cap=src->len;h->size+=sizeof(const char*)*dst->cap;sp_gc_bytes_add(sizeof(const char*)*dst->cap);}memcpy(dst->data,src->data,sizeof(const char*)*src->len);dst->len=src->len;}
const char*sp_StrArray_pop(sp_StrArray*a){SP_GC_ROOT(a);if(!a||a->len<=0)return NULL;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return NULL;}return a->data[--a->len];}
const char*sp_StrArray_shift(sp_StrArray*a){SP_GC_ROOT(a);if(!a||a->len<=0)return NULL;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return NULL;}const char*v=a->data[0];memmove(a->data,a->data+1,(size_t)(--a->len)*sizeof(const char*));return v;}
/* a[start, len] / a[start..end] for StrArray. Same negative-start and
   length-clamping semantics as sp_IntArray_slice. */
sp_StrArray*sp_StrArray_slice(sp_StrArray*a,sp_int start,sp_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_StrArray*b=sp_StrArray_new();if(start>=a->len||len<=0)return b;if(len>a->len-start)len=a->len-start;for(sp_int i=0;i<len;i++)sp_StrArray_push(b,a->data[start+i]);return b;}
sp_StrArray*sp_StrArray_slice_range(sp_StrArray*a,sp_int start,sp_int end_,sp_int excl){SP_GC_ROOT(a);if(end_<0)end_+=a->len;if(start<0)start+=a->len;if(start<0||start>a->len)return NULL;/* a start outside [-len, len] is nil (#4524); start == len is [] */sp_int n=end_-start+(excl?0:1);if(n<0)n=0;return sp_StrArray_slice(a,start,n);}
void sp_StrArray_reverse_bang(sp_StrArray*a){SP_GC_ROOT(a); sp_gc_wb((void*)a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}for(sp_int i=0,j=a->len-1;i<j;i++,j--){const char*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
void sp_StrArray_rotate_bang(sp_StrArray*a,sp_int n){SP_GC_ROOT(a);
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}
  if(a->len<=0)return;
  n=((n%a->len)+a->len)%a->len;
  if(n==0)return;
  const char**d=a->data;
  sp_int rest=a->len-n;
  sp_int keep=n<rest?n:rest;
  const char* stackbuf[32];
  const char**t=keep<=32?stackbuf:(const char**)sp_pl_alloc(sizeof(const char*)*(size_t)keep);
  if(!t)sp_oom_die();
  if(n<=rest){
    memcpy(t,d,sizeof(const char*)*(size_t)n);
    memmove(d,d+n,sizeof(const char*)*(size_t)rest);
    memcpy(d+rest,t,sizeof(const char*)*(size_t)n);
  }
  else{
    memcpy(t,d+n,sizeof(const char*)*(size_t)rest);
    memmove(d+rest,d,sizeof(const char*)*(size_t)n);
    memcpy(d,t,sizeof(const char*)*(size_t)rest);
  }
  if(t!=stackbuf)sp_pl_free(t);
}
static int _sp_str_cmp(const void*a,const void*b){return sp_str_cmp_bytes(*(const char*const*)a,*(const char*const*)b);}
void sp_StrArray_sort_bang(sp_StrArray*a){SP_GC_ROOT(a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}qsort(a->data,a->len,sizeof(const char*),_sp_str_cmp);}
void sp_StrArray_uniq_bang(sp_StrArray*a){SP_GC_ROOT(a); sp_gc_wb((void*)a);if(!a||a->frozen){if(a&&a->frozen)sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}for(sp_int i=0;i<a->len;){int dup=0;for(sp_int j=0;j<i;j++){if(a->data[j]==a->data[i]||(a->data[j]&&a->data[i]&&!sp_str_cmp_bytes(a->data[j],a->data[i]))){dup=1;break;}}if(dup){for(sp_int k2=i;k2<a->len-1;k2++)a->data[k2]=a->data[k2+1];a->len--;}
else i++;}}
sp_StrArray*sp_StrArray_uniq(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*b=sp_StrArray_new();if(!a)return b;for(sp_int i=0;i<a->len;i++){int found=0;for(sp_int j=0;j<b->len;j++){if(b->data[j]==a->data[i]||(b->data[j]&&a->data[i]&&!sp_str_cmp_bytes(b->data[j],a->data[i]))){found=1;break;}}if(!found)sp_StrArray_push(b,a->data[i]);}return b;}
/* byte_len for both the separator and each element: an embedded NUL is a byte
   of the string, and strlen stopped at it (the opportunistic NUL policy). */
const char*sp_StrArray_join(sp_StrArray*a,const char*sep){if(!sep)sep="";if(!a)return sp_str_empty;size_t sl=sp_str_byte_len(sep),cap=256;char*buf=(char*)sp_pl_alloc(cap);size_t len=0;for(sp_int i=0;i<a->len;i++){if(i>0){if(len+sl>=cap){cap*=2;buf=(char*)sp_pl_realloc(buf,cap);}memcpy(buf+len,sep,sl);len+=sl;}const char*_e=a->data[i]?a->data[i]:"";size_t el=sp_str_byte_len(_e);if(len+el>=cap){cap=((len+el)*2)+1;buf=(char*)sp_pl_realloc(buf,cap);}memcpy(buf+len,_e,el);len+=el;}buf[len]=0;char*r=sp_str_alloc(len);memcpy(r,buf,len);sp_str_set_len(r,len);sp_pl_free(buf);/* the joined bytes are binary if any part was: see sp_str_bin_from */if(sp_str_is_binary(sep))sp_str_mark_binary(r);for(sp_int i=0;i<a->len;i++)if(a->data[i]&&sp_str_is_binary(a->data[i])){sp_str_mark_binary(r);break;}return r;}
sp_bool sp_StrArray_include(sp_StrArray*a,const char*v){SP_GC_ROOT(a);SP_GC_ROOT_STR(v);if(!a)return FALSE;for(sp_int i=0;i<a->len;i++)if(sp_str_cmp_bytes(a->data[i],v)==0)return TRUE;return FALSE;}
sp_StrArray*sp_StrArray_intersect(sp_StrArray*a,sp_StrArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_StrArray*r=sp_StrArray_new();if(!a||!b)return r;for(sp_int i=0;i<a->len;i++){const char*v=a->data[i];if(sp_StrArray_include(b,v)&&!sp_StrArray_include(r,v))sp_StrArray_push(r,v);}return r;}
sp_bool sp_StrArray_intersect_p(sp_StrArray*a,sp_StrArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);if(!a||!b)return 0;for(sp_int i=0;i<a->len;i++)if(sp_StrArray_include(b,a->data[i]))return 1;return 0;}
sp_StrArray*sp_StrArray_union(sp_StrArray*a,sp_StrArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_StrArray*r=sp_StrArray_new();if(a)for(sp_int i=0;i<a->len;i++){const char*v=a->data[i];if(!sp_StrArray_include(r,v))sp_StrArray_push(r,v);}if(b){for(sp_int i=0;i<b->len;i++){const char*v=b->data[i];if(!sp_StrArray_include(r,v))sp_StrArray_push(r,v);}}return r;}
sp_StrArray*sp_StrArray_difference(sp_StrArray*a,sp_StrArray*b){SP_GC_ROOT(a);SP_GC_ROOT(b);sp_StrArray*r=sp_StrArray_new();for(sp_int i=0;i<a->len;i++){const char*v=a->data[i];if(!sp_StrArray_include(b,v))sp_StrArray_push(r,v);}return r;}
/* min/max by String#<=> (byte comparison via strcmp); NULL (nil) when empty.
   nil (NULL) elements are skipped so a holey/sparse array can't crash strcmp. */
const char*sp_StrArray_min(sp_StrArray*a){if(!a||a->len<=0)return NULL;const char*m=NULL;for(sp_int i=0;i<a->len;i++){const char*x=a->data[i];if(x&&(!m||sp_str_cmp_bytes(x,m)<0))m=x;}return m;}
const char*sp_StrArray_max(sp_StrArray*a){if(!a||a->len<=0)return NULL;const char*m=NULL;for(sp_int i=0;i<a->len;i++){const char*x=a->data[i];if(x&&(!m||sp_str_cmp_bytes(x,m)>0))m=x;}return m;}
sp_int sp_StrArray_index(sp_StrArray*a,const char*v){SP_GC_ROOT(a);SP_GC_ROOT_STR(v);for(sp_int i=0;i<a->len;i++)if(sp_str_cmp_bytes(a->data[i],v)==0)return i;return -1;}
sp_int sp_StrArray_rindex(sp_StrArray*a,const char*v){SP_GC_ROOT(a);SP_GC_ROOT_STR(v);for(sp_int i=a->len-1;i>=0;i--)if(sp_str_cmp_bytes(a->data[i],v)==0)return i;return -1;}
sp_StrArray*sp_StrArray_compact(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*r=sp_StrArray_new();if(!a)return r;for(sp_int i=0;i<a->len;i++)if(a->data[i]!=NULL)sp_StrArray_push(r,a->data[i]);return r;}
/* A scalar array cannot nest, but it can hold the nil a nullable read left
   behind (the SP_INT_NIL / NaN sentinel), so compact has to filter. */
sp_IntArray*sp_IntArray_compact(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*r=sp_IntArray_new();if(!a)return r;for(sp_int i=0;i<a->len;i++){sp_int v=a->data[a->start+i];if(v!=SP_INT_NIL)sp_IntArray_push(r,v);}return r;}
sp_FloatArray*sp_FloatArray_compact(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*r=sp_FloatArray_new();if(!a)return r;for(sp_int i=0;i<a->len;i++){sp_float v=a->data[i];if(!sp_float_is_nil(v))sp_FloatArray_push(r,v);}return r;}
const char*sp_StrArray_delete_at(sp_StrArray*a,sp_int i){SP_GC_ROOT(a); sp_gc_wb((void*)a);if(!a)return NULL;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return NULL;}if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;const char*v=a->data[i];for(sp_int j=i;j<a->len-1;j++)a->data[j]=a->data[j+1];a->len--;return v;}
const char*sp_StrArray_delete(sp_StrArray*a,const char*v){SP_GC_ROOT(a);SP_GC_ROOT_STR(v); sp_gc_wb((void*)a);if(!a)return NULL;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return NULL;}sp_int w=0;const char*found=NULL;for(sp_int i=0;i<a->len;i++){if(sp_str_cmp_bytes(a->data[i],v)!=0){a->data[w]=a->data[i];w++;}
else{found=a->data[i];}}a->len=w;return found;}
void sp_StrArray_insert(sp_StrArray*a,sp_int i,const char*v){SP_GC_ROOT(a);SP_GC_ROOT_STR(v); sp_gc_wb((void*)a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}sp_int orig=i;if(i<0)i+=a->len+1;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)(-(a->len+1))));while(i>a->len)sp_StrArray_push(a,NULL);/* CRuby pads with nils past the end */sp_StrArray_push(a,sp_str_empty);for(sp_int j=a->len-1;j>i;j--)a->data[j]=a->data[j-1];a->data[i]=v;}
void sp_StrArray_shuffle_bang(sp_StrArray*a){SP_GC_ROOT(a); sp_gc_wb((void*)a);if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}for(sp_int i=a->len-1;i>0;i--){sp_int j=sp_krand_below(i+1);const char*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
sp_StrArray*sp_StrArray_dup(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*r=sp_StrArray_new();sp_StrArray_replace(r,a);return r;}
sp_StrArray*sp_StrArray_sort(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*b=sp_StrArray_dup(a);sp_StrArray_sort_bang(b);return b;}
sp_StrArray*sp_StrArray_shuffle(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*r=sp_StrArray_new();sp_StrArray_replace(r,a);sp_StrArray_shuffle_bang(r);return r;}
const char *sp_StrArray_sample(sp_StrArray*a){SP_GC_ROOT(a);if(a->len<=0)return sp_str_empty;return a->data[sp_krand_below(a->len)];}

/* ============ poly/inspect-dependent array ops (display, concat, to_poly) ============ */
sp_StrArray *sp_StrArray_from_string_range(const char *s, const char *e, sp_int excl) {SP_GC_ROOT_STR(s);SP_GC_ROOT_STR(e);
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  if (!s || !e) return a;
  /* `cur` walks the range via String#succ, allocating a fresh heap string each
     step; the next sp_str_alloc can trigger a GC that would sweep both the
     array under construction and the current (unrooted) succ string, so the
     strcpy read freed memory (#3152). Root the slot -- it tracks each succ
     reassignment. */
  const char *cur = s;
  SP_GC_ROOT_STR(cur);
  int iters = 0;
  /* Two all-digit endpoints walk numerically, so ("9".."11") holds "9", "10",
     "11" -- a plain byte compare stops at once because "9" > "1" (#3549).
     Anything else keeps the byte order, where ("y".."ab") is empty. */
  size_t elen = strlen(e);
  int numeric = *s && *e;
  for (const char *q = s; numeric && *q; q++) if (*q < '0' || *q > '9') numeric = 0;
  for (const char *q = e; numeric && *q; q++) if (*q < '0' || *q > '9') numeric = 0;
  while (iters < 4096) {
    size_t clen = strlen(cur);
    int cmp = (numeric && clen != elen) ? (clen < elen ? -1 : 1)
                                        : sp_str_cmp_bytes(cur, e);
    if (cmp > 0) break;
    if (cmp == 0 && excl) break;
    char *copy = sp_str_alloc(strlen(cur));
    strcpy(copy, cur);
    sp_StrArray_push(a, copy);
    if (cmp == 0) break;
    cur = sp_str_succ(cur);
    iters++;
  }
  return a;
}
const char*sp_IntArray_inspect(sp_IntArray*a){SP_GC_ROOT(a);return a?sp_inspect_container(sp_box_obj(a,SP_BUILTIN_INT_ARRAY)):"nil";}
const char*sp_FloatArray_inspect(sp_FloatArray*a){SP_GC_ROOT(a);return a?sp_inspect_container(sp_box_obj(a,SP_BUILTIN_FLT_ARRAY)):"nil";}
const char*sp_FloatArray_join(sp_FloatArray*a,const char*sep){if(!sep)sep="";SP_GC_ROOT(a);size_t sl=sp_str_byte_len(sep),cap=256;char*buf=(char*)sp_pl_alloc(cap);size_t len=0;if(a){for(sp_int i=0;i<a->len;i++){if(i>0){if(len+sl>=cap){cap*=2;buf=(char*)sp_pl_realloc(buf,cap);}memcpy(buf+len,sep,sl);len+=sl;}const char*es=sp_float_to_s(a->data[i]);size_t el=strlen(es);if(len+el>=cap){while(len+el>=cap)cap*=2;buf=(char*)sp_pl_realloc(buf,cap);}memcpy(buf+len,es,el);len+=el;}}buf[len]=0;char*r=sp_str_alloc(len);memcpy(r,buf,len);sp_str_set_len(r,len);sp_pl_free(buf);return r;}
sp_bool sp_FloatArray_eq(sp_FloatArray*a,sp_FloatArray*b){if(!a||!b)return a==b;if(a->len!=b->len)return FALSE;for(sp_int i=0;i<a->len;i++)if(a->data[i]!=b->data[i])return FALSE;return TRUE;}
const char*sp_StrArray_inspect(sp_StrArray*a){SP_GC_ROOT(a);return a?sp_inspect_container(sp_box_obj(a,SP_BUILTIN_STR_ARRAY)):"nil";}
const char*sp_PtrArray_inspect(sp_PtrArray*a){if(!a)return SPL("nil");SP_GC_ROOT(a);sp_String*s=sp_String_new("[");SP_GC_ROOT(s);for(sp_int i=0;i<a->len;i++){if(i>0)sp_String_append(s,", ");sp_String_append(s,"#<Object>");}sp_String_append(s,"]");return sp_str_dup(s->data);}
/* Array#slice_before(delim): start a new chunk before each element == delim. */
sp_PtrArray*sp_IntArray_slice_before(sp_IntArray*a,sp_int d){SP_GC_ROOT(a);sp_PtrArray*out=sp_PtrArray_new();SP_GC_ROOT(out);if(!a)return out;sp_IntArray*cur=sp_IntArray_new();SP_GC_ROOT(cur);for(sp_int i=0;i<a->len;i++){sp_int e=a->data[a->start+i];if(e==d&&cur->len>0){sp_PtrArray_push(out,cur);cur=sp_IntArray_new();}sp_IntArray_push(cur,e);}if(cur->len>0)sp_PtrArray_push(out,cur);return out;}
/* Array#slice_after(delim): end a chunk after each element == delim. */
sp_PtrArray*sp_IntArray_slice_after(sp_IntArray*a,sp_int d){SP_GC_ROOT(a);sp_PtrArray*out=sp_PtrArray_new();SP_GC_ROOT(out);if(!a)return out;sp_IntArray*cur=sp_IntArray_new();SP_GC_ROOT(cur);for(sp_int i=0;i<a->len;i++){sp_int e=a->data[a->start+i];sp_IntArray_push(cur,e);if(e==d){sp_PtrArray_push(out,cur);cur=sp_IntArray_new();}}if(cur->len>0)sp_PtrArray_push(out,cur);return out;}
sp_PtrArray *sp_IntArray_product(sp_IntArray *a, sp_IntArray *b) {
  SP_GC_ROOT(a); SP_GC_ROOT(b);
  sp_PtrArray *out = sp_PtrArray_new();
  SP_GC_ROOT(out);
  if (!a || !b) return out;
  for (sp_int i = 0; i < a->len; i++) {
    for (sp_int j = 0; j < b->len; j++) {
      sp_IntArray *pair = sp_IntArray_new();
      sp_IntArray_push(pair, a->data[a->start + i]);
      sp_IntArray_push(pair, b->data[b->start + j]);
      sp_PtrArray_push(out, pair);
    }
  }
  return out;
}
const char*sp_PtrArray_str_join(sp_PtrArray*a,const char*sep){SP_GC_ROOT(a);SP_GC_ROOT_STR(sep);sp_int al=a->len;if(al==0)return sp_str_empty;size_t sl=strlen(sep),total=0;for(sp_int i=0;i<al;i++){if(i>0)total+=sl;sp_String*s=(sp_String*)a->data[i];if(s)total+=(size_t)s->len;}char*r=sp_str_alloc(total);size_t cur=0;for(sp_int i=0;i<al;i++){if(i>0){memcpy(r+cur,sep,sl);cur+=sl;}sp_String*s=(sp_String*)a->data[i];if(s&&s->len){memcpy(r+cur,s->data,(size_t)s->len);cur+=(size_t)s->len;}}return r;}
sp_RbVal sp_IntArray_index_poly(sp_IntArray *a, sp_int v)         {SP_GC_ROOT(a); sp_int n = sp_IntArray_index(a, v);   return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_IntArray_rindex_poly(sp_IntArray *a, sp_int v)        {SP_GC_ROOT(a); sp_int n = sp_IntArray_rindex(a, v);  return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_StrArray_index_poly(sp_StrArray *a, const char *v)     {SP_GC_ROOT(a);SP_GC_ROOT_STR(v); sp_int n = sp_StrArray_index(a, v);   return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_StrArray_rindex_poly(sp_StrArray *a, const char *v)    {SP_GC_ROOT(a);SP_GC_ROOT_STR(v); sp_int n = sp_StrArray_rindex(a, v);  return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_int sp_IntArray_index_opt(sp_IntArray *a, sp_int v)           {SP_GC_ROOT(a); sp_int n = sp_IntArray_index(a, v);   return n < 0 ? SP_INT_NIL : n; }
sp_int sp_IntArray_rindex_opt(sp_IntArray *a, sp_int v)          {SP_GC_ROOT(a); sp_int n = sp_IntArray_rindex(a, v);  return n < 0 ? SP_INT_NIL : n; }
sp_RbVal sp_FloatArray_index_poly(sp_FloatArray *a, sp_float v)   {SP_GC_ROOT(a); sp_int n = sp_FloatArray_index(a, v);  return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_FloatArray_rindex_poly(sp_FloatArray *a, sp_float v)  {SP_GC_ROOT(a); sp_int n = sp_FloatArray_rindex(a, v); return n < 0 ? sp_box_nil() : sp_box_int(n); }
/* A boxed needle compares only for a Float or Integer tag; every other tag
   misses, never riding a to-f coercion that would have parsed a String or
   interned a Symbol into a false hit -- and delete into a lost element.
   Bignum, Rational and Complex miss too, where CRuby's == can match: the
   family-wide trade, the Int arm fails the C build on the same shapes. */
sp_RbVal sp_FloatArray_index_key(sp_FloatArray *a, sp_RbVal v)  {SP_GC_ROOT(a); if (v.tag == SP_TAG_FLT) return sp_FloatArray_index_poly(a, v.v.f); if (v.tag == SP_TAG_INT) return sp_FloatArray_index_poly(a, (sp_float)v.v.i); return sp_box_nil(); }
sp_RbVal sp_FloatArray_rindex_key(sp_FloatArray *a, sp_RbVal v) {SP_GC_ROOT(a); if (v.tag == SP_TAG_FLT) return sp_FloatArray_rindex_poly(a, v.v.f); if (v.tag == SP_TAG_INT) return sp_FloatArray_rindex_poly(a, (sp_float)v.v.i); return sp_box_nil(); }
sp_float sp_FloatArray_delete_key(sp_FloatArray *a, sp_RbVal v) {SP_GC_ROOT(a); if (v.tag == SP_TAG_FLT) return sp_FloatArray_delete(a, v.v.f); if (v.tag == SP_TAG_INT) return sp_FloatArray_delete(a, (sp_float)v.v.i); return sp_float_nil(); }
const int64_t *sp_IntArray_ffi_data(sp_IntArray *a) { return a ? (const int64_t *)(a->data + a->start) : (const int64_t *)0; }
const double  *sp_FloatArray_ffi_data(sp_FloatArray *a) { return a ? (const double *)a->data : (const double *)0; }
sp_IntArray *sp_IntArray_concat(sp_IntArray *a, sp_IntArray *b) { SP_GC_ROOT(a); SP_GC_ROOT(b); sp_IntArray *r = sp_IntArray_new(); SP_GC_ROOT(r); if (a) for (sp_int i = 0; i < a->len; i++) sp_IntArray_push(r, sp_IntArray_get(a, i)); if (b) for (sp_int i = 0; i < b->len; i++) sp_IntArray_push(r, sp_IntArray_get(b, i)); return r; }
sp_StrArray *sp_StrArray_concat(sp_StrArray *a, sp_StrArray *b) {SP_GC_ROOT(a);SP_GC_ROOT(b); sp_StrArray *r = sp_StrArray_new(); SP_GC_ROOT(r); if (a) for (sp_int i = 0; i < a->len; i++) sp_StrArray_push(r, sp_StrArray_get(a, i)); if (b) for (sp_int i = 0; i < b->len; i++) sp_StrArray_push(r, sp_StrArray_get(b, i)); return r; }
sp_FloatArray *sp_FloatArray_concat(sp_FloatArray *a, sp_FloatArray *b) {SP_GC_ROOT(a);SP_GC_ROOT(b); sp_FloatArray *r = sp_FloatArray_new(); SP_GC_ROOT(r); if (a) for (sp_int i = 0; i < a->len; i++) sp_FloatArray_push(r, sp_FloatArray_get(a, i)); if (b) for (sp_int i = 0; i < b->len; i++) sp_FloatArray_push(r, sp_FloatArray_get(b, i)); return r; }
sp_PolyArray *sp_IntArray_to_poly(sp_IntArray *a) {
  SP_GC_ROOT(a);
  sp_PolyArray *r = sp_PolyArray_new();
  SP_GC_ROOT(r);
  if (!a) return r;
  for (sp_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_box_int(a->data[a->start + i]));
  return r;
}
sp_PolyArray *sp_StrArray_to_poly_fmt(sp_StrArray *a) {SP_GC_ROOT(a);
  sp_PolyArray *r = sp_PolyArray_new();
  if (!a) return r;
  for (sp_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_box_str(a->data[i]));
  return r;
}
sp_PolyArray *sp_FloatArray_to_poly(sp_FloatArray *a) {
  SP_GC_ROOT(a);
  sp_PolyArray *r = sp_PolyArray_new();
  SP_GC_ROOT(r);
  if (!a) return r;
  for (sp_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_box_float(a->data[i]));
  return r;
}
sp_IntArray *sp_IntArray_slice_bang(sp_IntArray *a, sp_int from, sp_int n) {SP_GC_ROOT(a);
  if (!a) return sp_IntArray_new();
  if (a->frozen) { sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY); return sp_IntArray_new(); }
  /* a start past the end (or before it after normalizing) is nil, not an
     empty slice: only start == len answers [] (#3607) */
  if (from < 0) from += a->len;
  if (from < 0 || from > a->len) return NULL;
  if (n < 0) return NULL;   /* slice!(start, -1) is nil, not an empty slice */
  if (from + n > a->len) n = a->len - from;
  sp_IntArray *r = sp_IntArray_new();
  for (sp_int i = 0; i < n; i++) sp_IntArray_push(r, a->data[a->start + from + i]);
  if (from == 0) {
    a->start += n;
    a->len -= n;
  }
else {
    for (sp_int i = from; i + n < a->len; i++) a->data[a->start + i] = a->data[a->start + i + n];
    a->len -= n;
  }
  return r;
}
sp_FloatArray *sp_FloatArray_slice_bang(sp_FloatArray *a, sp_int from, sp_int n) {SP_GC_ROOT(a);
  if (!a) return sp_FloatArray_new();
  if (a->frozen) { sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY); return sp_FloatArray_new(); }
  /* a start past the end (or before it after normalizing) is nil, not an
     empty slice: only start == len answers [] (#3607) */
  if (from < 0) from += a->len;
  if (from < 0 || from > a->len) return NULL;
  if (n < 0) return NULL;   /* slice!(start, -1) is nil, not an empty slice */
  if (from + n > a->len) n = a->len - from;
  sp_FloatArray *r = sp_FloatArray_new();
  for (sp_int i = 0; i < n; i++) sp_FloatArray_push(r, a->data[from + i]);
  for (sp_int i = from; i + n < a->len; i++) a->data[i] = a->data[i + n];
  a->len -= n;
  return r;
}
sp_StrArray *sp_StrArray_slice_bang(sp_StrArray *a, sp_int from, sp_int n) {SP_GC_ROOT(a); sp_gc_wb((void*)a);
  if (!a) return sp_StrArray_new();
  if (a->frozen) { sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY); return sp_StrArray_new(); }
  /* a start past the end (or before it after normalizing) is nil, not an
     empty slice: only start == len answers [] (#3607) */
  if (from < 0) from += a->len;
  if (from < 0 || from > a->len) return NULL;
  if (n < 0) return NULL;   /* slice!(start, -1) is nil, not an empty slice */
  if (from + n > a->len) n = a->len - from;
  sp_StrArray *r = sp_StrArray_new();
  for (sp_int i = 0; i < n; i++) sp_StrArray_push(r, a->data[from + i]);
  for (sp_int i = from; i + n < a->len; i++) a->data[i] = a->data[i + n];
  a->len -= n;
  return r;
}
sp_PtrArray *sp_PtrArray_slice_bang(sp_PtrArray *a, sp_int from, sp_int n) {SP_GC_ROOT(a); sp_gc_wb((void*)a);
  if (!a) return sp_PtrArray_new_scan(NULL);
  if (a->frozen) { sp_raise_frozen_array_at(a, SP_BUILTIN_PTR_ARRAY); return sp_PtrArray_new_scan(a->scan_elem); }
  /* a start past the end (or before it after normalizing) is nil, not an
     empty slice: only start == len answers [] (#3607) */
  if (from < 0) from += a->len;
  if (from < 0 || from > a->len) return NULL;
  if (n < 0) return NULL;   /* slice!(start, -1) is nil, not an empty slice */
  if (from + n > a->len) n = a->len - from;
  sp_PtrArray *r = sp_PtrArray_new_scan(a->scan_elem);
  for (sp_int i = 0; i < n; i++) sp_PtrArray_push(r, a->data[from + i]);
  for (sp_int i = from; i + n < a->len; i++) a->data[i] = a->data[i + n];
  a->len -= n;
  return r;
}

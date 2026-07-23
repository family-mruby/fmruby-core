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
   `(1..MRB_INT_MAX).to_a` overflows the realloc size_t to a tiny number,
   then writes past the allocation. */
sp_IntArray*sp_IntArray_from_range(mrb_int s,mrb_int e){sp_IntArray*a=sp_IntArray_new();mrb_int n=e-s+1;if(n<0)n=0;if(n>(mrb_int)(1LL<<30))n=(mrb_int)(1LL<<30);if(n>a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*a->cap);h->size-=sizeof(mrb_int)*a->cap;a->cap=n;a->data=(mrb_int*)realloc(a->data,sizeof(mrb_int)*a->cap);h->size+=sizeof(mrb_int)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*a->cap);}for(mrb_int i=0;i<n;i++)a->data[i]=s+i;a->len=n;return a;}
/* (beg..end).step(step) materialised as an IntArray. step==0 raises like
   CRuby; a negative step descends; exclusive ranges drop the endpoint. The
   loop tests the bound directly and guards v+=step, so a range near the
   mrb_int limits (e.g. an endless range's INTPTR_MAX end) cannot overflow --
   signed integer overflow is undefined behaviour in C. */
sp_IntArray*sp_IntArray_from_range_step(mrb_int beg,mrb_int end,mrb_int step,mrb_int excl){
  if(step==0)sp_raise_cls("ArgumentError","step can't be 0");
  sp_IntArray*a=sp_IntArray_new();
  if(step>0){
    for(mrb_int v=beg;v<end||(!excl&&v==end);){
      sp_IntArray_push(a,v);
      if(v>0&&step>INTPTR_MAX-v)break;
      v+=step;
    }
  }else{
    for(mrb_int v=beg;v>end||(!excl&&v==end);){
      sp_IntArray_push(a,v);
      if(v<0&&step<INTPTR_MIN-v)break;
      v+=step;
    }
  }
  return a;
}
sp_IntArray*sp_IntArray_dup(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*b=sp_IntArray_new();if(a->len>b->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)b-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*b->cap);h->size-=sizeof(mrb_int)*b->cap;b->cap=a->len;void*nd=realloc(b->data,sizeof(mrb_int)*b->cap);if(!nd)sp_oom_die();b->data=(mrb_int*)nd;h->size+=sizeof(mrb_int)*b->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*b->cap);}memcpy(b->data,a->data+a->start,sizeof(mrb_int)*a->len);b->len=a->len;return b;}
/* a[start, len] / a[start..end] for IntArray. Negative start counts from
   the end. start past the array length yields an empty result; len is
   clamped so we never read past the source. CRuby returns nil for
   out-of-bounds start; we return an empty IntArray since this typed
   collection has no nullable form. */
sp_IntArray*sp_IntArray_slice(sp_IntArray*a,mrb_int start,mrb_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_IntArray*b=sp_IntArray_new();if(start>=a->len||len<=0)return b;if(start+len>a->len)len=a->len-start;if(len>b->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)b-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*b->cap);h->size-=sizeof(mrb_int)*b->cap;b->cap=len;b->data=(mrb_int*)realloc(b->data,sizeof(mrb_int)*b->cap);h->size+=sizeof(mrb_int)*b->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*b->cap);}memcpy(b->data,a->data+a->start+start,sizeof(mrb_int)*len);b->len=len;return b;}
/* a[start..end] / a[start...end] with possibly negative endpoints.
   Normalize end against a->len first; the bare _slice already handles
   negative start. Issue #496. */
sp_IntArray*sp_IntArray_slice_range(sp_IntArray*a,mrb_int start,mrb_int end_,mrb_int excl){if(end_<0)end_+=a->len;if(start<0)start+=a->len;mrb_int n=end_-start+(excl?0:1);if(n<0||start<0)n=0;return sp_IntArray_slice(a,start,n);}
void sp_IntArray_replace(sp_IntArray*dst,sp_IntArray*src){dst->len=0;dst->start=0;if(src->len>dst->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)dst-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*dst->cap);h->size-=sizeof(mrb_int)*dst->cap;void*nd=realloc(dst->data,sizeof(mrb_int)*src->len);if(!nd){perror("realloc");exit(1);}dst->data=(mrb_int*)nd;dst->cap=src->len;h->size+=sizeof(mrb_int)*dst->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*dst->cap);}memcpy(dst->data,src->data+src->start,sizeof(mrb_int)*src->len);dst->len=src->len;}
/* arr[start,len] = src / arr[range] = src : remove `len` elements at `start`
   and insert the `srcn` elements of `src` in their place, shifting the tail.
   Reuses push so capacity growth + GC byte accounting stay in one place. src
   is snapshotted first so splicing an array into itself (`a[1,1]=a`) is safe.
   A start beyond the end would need a nil gap, which a typed array cannot
   hold, so it raises (the poly-array splice fills nil instead). */
void sp_IntArray_splice(sp_IntArray*a,mrb_int start,mrb_int len,const mrb_int*src,mrb_int srcn){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array();return;}
  SP_GC_ROOT(a);
  mrb_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  if(s>alen){sp_raise_cls("RuntimeError",sp_sprintf("index %lld out of range for typed-array splice (would require nil fill)",(long long)s));return;}
  if(s+len>alen)len=alen-s;
  /* Equal-length replacement is a pure overwrite: no length change, no
     capacity growth, no tail shift. memmove, since src may alias a's own
     buffer (self-splice). This is the hot shape (optcarrot's per-tile
     `@bg_pixels[x, 8] = <8-elem row>`); the general path below allocates. */
  if(len==srcn){if(srcn>0)memmove(a->data+a->start+s,src,sizeof(mrb_int)*(size_t)srcn);return;}
  mrb_int*sb=NULL;
  if(srcn>0){sb=(mrb_int*)malloc(sizeof(mrb_int)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,src,sizeof(mrb_int)*(size_t)srcn);}
  mrb_int tail_from=s+len,tail_n=alen-tail_from;
  mrb_int*tb=NULL;
  if(tail_n>0){tb=(mrb_int*)malloc(sizeof(mrb_int)*(size_t)tail_n);if(!tb){free(sb);sp_oom_die();}memcpy(tb,a->data+a->start+tail_from,sizeof(mrb_int)*(size_t)tail_n);}
  a->len=s;
  for(mrb_int i=0;i<srcn;i++)sp_IntArray_push(a,sb[i]);
  for(mrb_int i=0;i<tail_n;i++)sp_IntArray_push(a,tb[i]);
  free(sb);free(tb);
}
void sp_FloatArray_splice(sp_FloatArray*a,mrb_int start,mrb_int len,const mrb_float*src,mrb_int srcn){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array();return;}
  SP_GC_ROOT(a);
  mrb_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  if(s>alen){sp_raise_cls("RuntimeError",sp_sprintf("index %lld out of range for typed-array splice (would require nil fill)",(long long)s));return;}
  if(s+len>alen)len=alen-s;
  if(len==srcn){if(srcn>0)memmove(a->data+s,src,sizeof(mrb_float)*(size_t)srcn);return;}  /* see the int form */
  mrb_float*sb=NULL;
  if(srcn>0){sb=(mrb_float*)malloc(sizeof(mrb_float)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,src,sizeof(mrb_float)*(size_t)srcn);}
  mrb_int tail_from=s+len,tail_n=alen-tail_from;
  mrb_float*tb=NULL;
  if(tail_n>0){tb=(mrb_float*)malloc(sizeof(mrb_float)*(size_t)tail_n);if(!tb){free(sb);sp_oom_die();}memcpy(tb,a->data+tail_from,sizeof(mrb_float)*(size_t)tail_n);}
  a->len=s;
  for(mrb_int i=0;i<srcn;i++)sp_FloatArray_push(a,sb[i]);
  for(mrb_int i=0;i<tail_n;i++)sp_FloatArray_push(a,tb[i]);
  free(sb);free(tb);
}
void sp_StrArray_splice(sp_StrArray*a,mrb_int start,mrb_int len,const char*const*src,mrb_int srcn){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array();return;}
  SP_GC_ROOT(a);
  mrb_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  if(s>alen){sp_raise_cls("RuntimeError",sp_sprintf("index %lld out of range for typed-array splice (would require nil fill)",(long long)s));return;}
  if(s+len>alen)len=alen-s;
  if(len==srcn){if(srcn>0)memmove(a->data+s,src,sizeof(const char*)*(size_t)srcn);return;}  /* see the int form */
  const char**sb=NULL;
  if(srcn>0){sb=(const char**)malloc(sizeof(const char*)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,src,sizeof(const char*)*(size_t)srcn);}
  /* Snapshot the tail into a *rooted* holder before truncating a->len: once
     a->len shrinks, a no longer scans the tail, so a raw buffer would leave the
     tail's GC strings unrooted across the pushes below (which can collect). The
     root sits at function scope -- not inside the guard -- so it stays live for
     the whole splice. */
  mrb_int tail_from=s+len,tail_n=alen-tail_from;
  sp_StrArray*tb=NULL;SP_GC_ROOT(tb);
  if(tail_n>0){tb=sp_StrArray_new();for(mrb_int i=0;i<tail_n;i++)sp_StrArray_push(tb,a->data[tail_from+i]);}
  a->len=s;
  for(mrb_int i=0;i<srcn;i++)sp_StrArray_push(a,sb[i]);
  if(tb)for(mrb_int i=0;i<tb->len;i++)sp_StrArray_push(a,tb->data[i]);
  free(sb);
}
/* poly-array splice: like the typed forms but elements are boxed, so a nil gap
   (start past the end) is filled with nil rather than raising. Ruby's `a[s,l]=`
   splices the RHS's elements when it is an Array, else inserts it as one
   element -- a runtime fact decided here from src's class id. */
void sp_PolyArray_splice(sp_PolyArray*a,mrb_int start,mrb_int len,sp_RbVal src){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array();return;}
  SP_GC_ROOT(a);
  /* src rooted too: the pushes below can trigger a collection while some of
     the snapshotted elements are reachable only through src. */
  SP_GC_ROOT_RBVAL(src);
  mrb_int alen=a->len,s=start;
  if(s<0)s+=alen;
  if(len<0){sp_raise_cls("IndexError",sp_sprintf("negative length (%lld)",(long long)len));return;}
  if(s<0){sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)start,(long long)-alen));return;}
  /* snapshot the source elements as boxed values. src's class id decides
     array-vs-single-element (Ruby splices an Array RHS, inserts anything
     else). A user object with to_ary is coerced at COMPILE time when its
     static type is known; a to_ary object reaching here as a runtime poly
     value still inserts as one element -- closing that would need a
     codegen-installed dispatch hook (the sp_obj_cmp_hook pattern). */
  int src_is_array=src.tag==SP_TAG_OBJ&&(src.cls_id==SP_BUILTIN_INT_ARRAY||src.cls_id==SP_BUILTIN_FLT_ARRAY||src.cls_id==SP_BUILTIN_STR_ARRAY||src.cls_id==SP_BUILTIN_POLY_ARRAY);
  mrb_int srcn=0;sp_RbVal*sb=NULL;
  if(src_is_array){
    void*p=src.v.p;
    switch(src.cls_id){
      case SP_BUILTIN_INT_ARRAY:{sp_IntArray*x=(sp_IntArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)malloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(mrb_int i=0;i<srcn;i++)sb[i]=sp_box_int(x->data[x->start+i]);}break;}
      case SP_BUILTIN_FLT_ARRAY:{sp_FloatArray*x=(sp_FloatArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)malloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(mrb_int i=0;i<srcn;i++)sb[i]=sp_box_float(x->data[i]);}break;}
      case SP_BUILTIN_STR_ARRAY:{sp_StrArray*x=(sp_StrArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)malloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();for(mrb_int i=0;i<srcn;i++)sb[i]=sp_box_str(x->data[i]);}break;}
      case SP_BUILTIN_POLY_ARRAY:{sp_PolyArray*x=(sp_PolyArray*)p;srcn=x->len;if(srcn>0){sb=(sp_RbVal*)malloc(sizeof(sp_RbVal)*(size_t)srcn);if(!sb)sp_oom_die();memcpy(sb,x->data,sizeof(sp_RbVal)*(size_t)srcn);}break;}
      default:break;
    }
  }else{
    srcn=1;sb=(sp_RbVal*)malloc(sizeof(sp_RbVal));if(!sb)sp_oom_die();sb[0]=src;
  }
  /* clamp/gap: when start is past the end, the [s+len) tail is empty and the
     [alen,s) gap fills with nil */
  mrb_int gap=0,tail_from,tail_n;
  if(s<=alen){if(s+len>alen)len=alen-s;tail_from=s+len;tail_n=alen-tail_from;}
  else{gap=s-alen;tail_from=alen;tail_n=0;}
  /* Snapshot the tail into a *rooted* holder before truncating a->len: the tail
     elements belong to a and, once a->len shrinks, are reachable through neither
     a nor src, so a raw buffer would leave them unrooted across the pushes below
     (which can collect). The source snapshot sb needs no such holder -- its
     elements stay reachable through the already-rooted src. */
  sp_PolyArray*tb=NULL;SP_GC_ROOT(tb);
  if(tail_n>0){tb=sp_PolyArray_new();for(mrb_int i=0;i<tail_n;i++)sp_PolyArray_push(tb,a->data[tail_from+i]);}
  a->len=(s<=alen)?s:alen;
  for(mrb_int i=0;i<gap;i++)sp_PolyArray_push(a,sp_box_nil());
  for(mrb_int i=0;i<srcn;i++)sp_PolyArray_push(a,sb[i]);
  if(tb)for(mrb_int i=0;i<tb->len;i++)sp_PolyArray_push(a,tb->data[i]);
  free(sb);
}
void sp_IntArray_reverse_bang(sp_IntArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}for(mrb_int i=0,j=a->len-1;i<j;i++,j--){mrb_int t=a->data[a->start+i];a->data[a->start+i]=a->data[a->start+j];a->data[a->start+j]=t;}}
void sp_IntArray_rotate_bang(sp_IntArray*a,mrb_int n){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}if(a->len<=0)return;n=((n%a->len)+a->len)%a->len;if(n==0)return;mrb_int*d=a->data+a->start;mrb_int lo=0,hi=n-1;while(lo<hi){mrb_int t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=n;hi=a->len-1;while(lo<hi){mrb_int t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=0;hi=a->len-1;while(lo<hi){mrb_int t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}}
static int _sp_int_cmp(const void*a,const void*b){mrb_int va=*(const mrb_int*)a,vb=*(const mrb_int*)b;return(va>vb)-(va<vb);}
sp_IntArray*sp_IntArray_sort(sp_IntArray*a){sp_IntArray*b=sp_IntArray_dup(a);qsort(b->data+b->start,b->len,sizeof(mrb_int),_sp_int_cmp);return b;}
void sp_IntArray_sort_bang(sp_IntArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}qsort(a->data+a->start,a->len,sizeof(mrb_int),_sp_int_cmp);}
void sp_IntArray_uniq_bang(sp_IntArray*a){if(!a||a->frozen){if(a&&a->frozen)sp_raise_frozen_array();return;}for(mrb_int i=0;i<a->len;){int dup=0;for(mrb_int j=0;j<i;j++){if(a->data[a->start+j]==a->data[a->start+i]){dup=1;break;}}if(dup){for(mrb_int k2=i;k2<a->len-1;k2++)a->data[a->start+k2]=a->data[a->start+k2+1];a->len--;}else i++;}}
void sp_IntArray_shuffle_bang(sp_IntArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}for(mrb_int i=a->len-1;i>0;i--){mrb_int j=sp_krand_below(i+1);mrb_int t=a->data[a->start+i];a->data[a->start+i]=a->data[a->start+j];a->data[a->start+j]=t;}}
sp_IntArray*sp_IntArray_shuffle(sp_IntArray*a){sp_IntArray*b=sp_IntArray_dup(a);sp_IntArray_shuffle_bang(b);return b;}
/* Array#sample. CRuby returns nil for `[].sample`; in spinel's typed-array
   slot nil collapses to 0. sp_krand_below guards the len==0 draw itself.
   Issue #536. */
mrb_int sp_IntArray_sample(sp_IntArray*a){if(a->len<=0)return 0;return a->data[a->start+sp_krand_below(a->len)];}
/* Issue #745/#832: empty min/max return SP_INT_NIL (caller treats as
   int?); without the guard, the first read is uninitialized memory. */
mrb_int sp_IntArray_min(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;mrb_int m=a->data[a->start];for(mrb_int i=1;i<a->len;i++)if(a->data[a->start+i]<m)m=a->data[a->start+i];return m;}
mrb_int sp_IntArray_max(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;mrb_int m=a->data[a->start];for(mrb_int i=1;i<a->len;i++)if(a->data[a->start+i]>m)m=a->data[a->start+i];return m;}
mrb_int sp_IntArray_sum(sp_IntArray*a,mrb_int init){mrb_int s=init;for(mrb_int i=0;i<a->len;i++)s+=a->data[a->start+i];return s;}
mrb_bool sp_IntArray_include(sp_IntArray*a,mrb_int v){if(!a)return FALSE;for(mrb_int i=0;i<a->len;i++)if(a->data[a->start+i]==v)return TRUE;return FALSE;}
mrb_int sp_IntArray_index(sp_IntArray*a,mrb_int v){for(mrb_int i=0;i<a->len;i++)if(a->data[a->start+i]==v)return i;return -1;}
mrb_int sp_IntArray_rindex(sp_IntArray*a,mrb_int v){for(mrb_int i=a->len-1;i>=0;i--)if(a->data[a->start+i]==v)return i;return -1;}
mrb_int sp_IntArray_delete_at(sp_IntArray*a,mrb_int i){if(a&&a->frozen){sp_raise_frozen_array();return SP_INT_NIL;}if(i<0)i+=a->len;if(i<0||i>=a->len)return SP_INT_NIL;mrb_int v=a->data[a->start+i];for(mrb_int j=i;j<a->len-1;j++)a->data[a->start+j]=a->data[a->start+j+1];a->len--;return v;}
mrb_int sp_IntArray_delete(sp_IntArray*a,mrb_int v){if(a&&a->frozen){sp_raise_frozen_array();return 0;}mrb_int w=0;for(mrb_int i=0;i<a->len;i++){if(a->data[a->start+i]!=v){a->data[a->start+w]=a->data[a->start+i];w++;}}mrb_int d=a->len-w;a->len=w;return d>0?v:SP_INT_NIL;}  /* CRuby: nil when absent */
/* Issue #788: clamp i so a very-negative index doesn't underflow past
   a->start and write into the array's GC header. */
void sp_IntArray_insert(sp_IntArray*a,mrb_int i,mrb_int v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}mrb_int orig=i;if(i<0)i+=a->len+1;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)(-(a->len+1))));while(i>a->len)sp_IntArray_push(a,SP_INT_NIL);/* CRuby pads with nils past the end */sp_IntArray_push(a,0);for(mrb_int j=a->len-1;j>i;j--)a->data[a->start+j]=a->data[a->start+j-1];a->data[a->start+i]=v;}
sp_IntArray*sp_IntArray_uniq(sp_IntArray*a){SP_GC_ROOT(a);sp_IntArray*b=sp_IntArray_new();for(mrb_int i=0;i<a->len;i++){int found=0;for(mrb_int j=0;j<b->len;j++){if(b->data[b->start+j]==a->data[a->start+i]){found=1;break;}}if(!found)sp_IntArray_push(b,a->data[a->start+i]);}return b;}
sp_IntArray*sp_IntArray_intersect(sp_IntArray*a,sp_IntArray*b){sp_IntArray*r=sp_IntArray_new();if(!a||!b)return r;for(mrb_int i=0;i<a->len;i++){mrb_int v=a->data[a->start+i];if(sp_IntArray_include(b,v)&&!sp_IntArray_include(r,v))sp_IntArray_push(r,v);}return r;}
mrb_bool sp_IntArray_intersect_p(sp_IntArray*a,sp_IntArray*b){if(!a||!b)return 0;for(mrb_int i=0;i<a->len;i++)if(sp_IntArray_include(b,a->data[a->start+i]))return 1;return 0;}
sp_IntArray*sp_IntArray_union(sp_IntArray*a,sp_IntArray*b){sp_IntArray*r=sp_IntArray_new();if(a)for(mrb_int i=0;i<a->len;i++){mrb_int v=a->data[a->start+i];if(!sp_IntArray_include(r,v))sp_IntArray_push(r,v);}if(b){for(mrb_int i=0;i<b->len;i++){mrb_int v=b->data[b->start+i];if(!sp_IntArray_include(r,v))sp_IntArray_push(r,v);}}return r;}
/* Array#- / Array#difference: keep every LHS element NOT in RHS,
   preserving the LHS's duplicates. `[1,1,2,3] - [3]` is `[1,1,2]`. */
sp_IntArray*sp_IntArray_difference(sp_IntArray*a,sp_IntArray*b){sp_IntArray*r=sp_IntArray_new();if(!a)return r;for(mrb_int i=0;i<a->len;i++){mrb_int v=a->data[a->start+i];if(!sp_IntArray_include(b,v))sp_IntArray_push(r,v);}return r;}
void sp_IntArray_unshift(sp_IntArray*a,mrb_int v){if(a->frozen){sp_raise_frozen_array();return;}if(a->start>0){a->start--;a->data[a->start]=v;a->len++;}else{mrb_int e=a->len+1;if(e>a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*a->cap);h->size-=sizeof(mrb_int)*a->cap;a->cap=(((((a->cap*2)))))+1;a->data=(mrb_int*)realloc(a->data,sizeof(mrb_int)*a->cap);h->size+=sizeof(mrb_int)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*a->cap);}memmove(a->data+1,a->data,sizeof(mrb_int)*a->len);a->data[0]=v;a->len++;}}
const char*sp_IntArray_join(sp_IntArray*a,const char*sep){size_t sl=strlen(sep),cap=256;char*buf=(char*)malloc(cap);size_t len=0;for(mrb_int i=0;i<a->len;i++){if(i>0){if(len+sl>=cap){cap*=2;buf=(char*)realloc(buf,cap);}memcpy(buf+len,sep,sl);len+=sl;}char tmp[32];int n=snprintf(tmp,32,"%lld",(long long)a->data[a->start+i]);if(len+n>=cap){cap*=2;buf=(char*)realloc(buf,cap);}memcpy(buf+len,tmp,n);len+=n;}buf[len]=0;char*r=sp_str_alloc(len);memcpy(r,buf,len);free(buf);return r;}
mrb_bool sp_IntArray_eq(sp_IntArray*a,sp_IntArray*b){if(!a||!b)return a==b;if(a->len!=b->len)return FALSE;for(mrb_int i=0;i<a->len;i++)if(a->data[a->start+i]!=b->data[b->start+i])return FALSE;return TRUE;}
/* Array#<=> for IntArray. Lexicographic: per-element compare, shorter
   array sorts before longer if all common elements match
   (`[1,2] <=> [1,2,3] == -1`). NULL recv compares equal to NULL, lower
   than any non-NULL. */
mrb_int sp_IntArray_cmp(sp_IntArray*a,sp_IntArray*b){if(!a||!b)return a==b?0:(a?1:-1);mrb_int n=a->len<b->len?a->len:b->len;for(mrb_int i=0;i<n;i++){mrb_int av=a->data[a->start+i],bv=b->data[b->start+i];if(av<bv)return -1;if(av>bv)return 1;}if(a->len<b->len)return -1;if(a->len>b->len)return 1;return 0;}

/* ============================ sp_FloatArray ============================ */
void sp_FloatArray_unshift(sp_FloatArray*a,mrb_float v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}sp_FloatArray_push(a,0.0);if(a->len>1)memmove(&a->data[1],&a->data[0],(size_t)(a->len-1)*sizeof(mrb_float));a->data[0]=v;}
/* (beg..end).step(step) / Float#step materialised as a FloatArray, following
   CRuby's ruby_float_step: an epsilon-corrected element count (so float drift
   never drops or adds a value) with each value computed as beg + i*step rather
   than by repeated addition. Honours range exclusivity; step==0 raises
   ArgumentError like CRuby; the final value is clamped to end on overshoot. */
sp_FloatArray*sp_FloatArray_from_step(mrb_float beg,mrb_float end,mrb_float step,mrb_int excl){
  if(step==0.0)sp_raise_cls("ArgumentError","step can't be 0");
  /* CRuby rejects a NaN range bound at construction with this message; a NaN
     step is just as degenerate. Guard before any arithmetic so the count never
     becomes NaN (whose cast to mrb_int is undefined behaviour). */
  if(isnan(beg)||isnan(end)||isnan(step))sp_raise_cls("ArgumentError","bad value for range");
  sp_FloatArray*a=sp_FloatArray_new();
  if(isinf(step)){if(step>0?beg<=end:beg>=end)sp_FloatArray_push(a,beg);return a;}
  mrb_float n=(end-beg)/step;
  mrb_float err=(fabs(beg)+fabs(end)+fabs(end-beg))/fabs(step)*DBL_EPSILON;
  if(err>0.5)err=0.5;
  if(excl){
    if(n<=0)return a;
    n=(n<1)?0:floor(n-err);
    mrb_float d=((n+1)*step)+beg;
    if(beg<end){if(d<end)n++;}else if(beg>end){if(d>end)n++;}
  }else{
    if(n<0)return a;
    n=floor(n+err);
    mrb_float d=((n+1)*step)+beg;
    if(beg<end){if(d<=end)n++;}else if(beg>end){if(d>=end)n++;}
  }
  /* An infinite end (or an enormous span) drives n to infinity or past what can
     be materialised; CRuby loops forever there. Raise rather than cast a
     non-finite/huge double to mrb_int (undefined behaviour) and exhaust memory.
     The 1<<30 cap matches sp_IntArray_from_range's sanity bound. */
  if(isinf(n)||n>=(mrb_float)(1LL<<30))sp_raise_cls("RangeError","range too large to materialize");
  mrb_int count=(mrb_int)n+1;
  for(mrb_int i=0;i<count;i++){
    mrb_float d=((mrb_float)i*step)+beg;
    if(step>=0?end<d:d<end)d=end;
    sp_FloatArray_push(a,d);
  }
  return a;
}
mrb_float sp_FloatArray_min(sp_FloatArray*a){if(a->len==0)return 0;mrb_float m=a->data[0];for(mrb_int i=1;i<a->len;i++)if(a->data[i]<m)m=a->data[i];return m;}
mrb_float sp_FloatArray_max(sp_FloatArray*a){if(a->len==0)return 0;mrb_float m=a->data[0];for(mrb_int i=1;i<a->len;i++)if(a->data[i]>m)m=a->data[i];return m;}
/* Array#sum for floats: Kahan-Babuska-Neumaier compensated summation,
   matching CRuby (float addition isn't associative, so a naive fold of
   [0.1,0.2,0.3] yields 0.6000000000000001 instead of 0.6). `c` accumulates
   the low-order bits dropped at each add and is folded back at the end. */
mrb_float sp_FloatArray_sum(sp_FloatArray*a,mrb_float init){mrb_float s=init,c=0.0;for(mrb_int i=0;i<a->len;i++){mrb_float x=a->data[i],t=s+x;if(fabs(s)>=fabs(x))c+=(s-t)+x;else c+=(x-t)+s;s=t;}return s+c;}
void sp_FloatArray_replace(sp_FloatArray*dst,sp_FloatArray*src){dst->len=0;if(src->len>dst->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)dst-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_float)*dst->cap);h->size-=sizeof(mrb_float)*dst->cap;void*nd=realloc(dst->data,sizeof(mrb_float)*src->len);if(!nd){perror("realloc");exit(1);}dst->data=(mrb_float*)nd;dst->cap=src->len;h->size+=sizeof(mrb_float)*dst->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_float)*dst->cap);}memcpy(dst->data,src->data,sizeof(mrb_float)*src->len);dst->len=src->len;}
/* a[start, len] / a[start..end] for FloatArray. Same negative-start and
   length-clamping semantics as sp_IntArray_slice. */
sp_FloatArray*sp_FloatArray_slice(sp_FloatArray*a,mrb_int start,mrb_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_FloatArray*b=sp_FloatArray_new();if(start>=a->len||len<=0)return b;if(start+len>a->len)len=a->len-start;if(len>b->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)b-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_float)*b->cap);h->size-=sizeof(mrb_float)*b->cap;b->cap=len;b->data=(mrb_float*)realloc(b->data,sizeof(mrb_float)*b->cap);h->size+=sizeof(mrb_float)*b->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_float)*b->cap);}memcpy(b->data,a->data+start,sizeof(mrb_float)*len);b->len=len;return b;}
sp_FloatArray*sp_FloatArray_slice_range(sp_FloatArray*a,mrb_int start,mrb_int end_,mrb_int excl){if(end_<0)end_+=a->len;if(start<0)start+=a->len;mrb_int n=end_-start+(excl?0:1);if(n<0||start<0)n=0;return sp_FloatArray_slice(a,start,n);}
void sp_FloatArray_reverse_bang(sp_FloatArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}for(mrb_int i=0,j=a->len-1;i<j;i++,j--){mrb_float t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
void sp_FloatArray_rotate_bang(sp_FloatArray*a,mrb_int n){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}if(a->len<=0)return;n=((n%a->len)+a->len)%a->len;if(n==0)return;mrb_float*d=a->data;mrb_int lo=0,hi=n-1;while(lo<hi){mrb_float t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=n;hi=a->len-1;while(lo<hi){mrb_float t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=0;hi=a->len-1;while(lo<hi){mrb_float t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}}
static int _sp_float_cmp(const void*a,const void*b){mrb_float va=*(const mrb_float*)a,vb=*(const mrb_float*)b;return(va>vb)-(va<vb);}
void sp_FloatArray_sort_bang(sp_FloatArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}qsort(a->data,a->len,sizeof(mrb_float),_sp_float_cmp);}
void sp_FloatArray_shuffle_bang(sp_FloatArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}for(mrb_int i=a->len-1;i>0;i--){mrb_int j=sp_krand_below(i+1);mrb_float t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
sp_FloatArray*sp_FloatArray_dup(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*b=sp_FloatArray_new();sp_FloatArray_replace(b,a);return b;}
sp_FloatArray*sp_FloatArray_sort(sp_FloatArray*a){sp_FloatArray*b=sp_FloatArray_dup(a);sp_FloatArray_sort_bang(b);return b;}
sp_FloatArray*sp_FloatArray_shuffle(sp_FloatArray*a){sp_FloatArray*r=sp_FloatArray_new();sp_FloatArray_replace(r,a);sp_FloatArray_shuffle_bang(r);return r;}
mrb_float sp_FloatArray_sample(sp_FloatArray*a){if(a->len<=0)return 0.0;return a->data[sp_krand_below(a->len)];}
/* IEEE 754 == on mrb_float: NaN never matches; +0.0 == -0.0 (diverges from Float#eql?). */
mrb_bool sp_FloatArray_include(sp_FloatArray*a,mrb_float v){if(!a)return FALSE;for(mrb_int i=0;i<a->len;i++)if(a->data[i]==v)return TRUE;return FALSE;}
sp_FloatArray*sp_FloatArray_intersect(sp_FloatArray*a,sp_FloatArray*b){sp_FloatArray*r=sp_FloatArray_new();if(!a||!b)return r;for(mrb_int i=0;i<a->len;i++){mrb_float v=a->data[i];if(sp_FloatArray_include(b,v)&&!sp_FloatArray_include(r,v))sp_FloatArray_push(r,v);}return r;}
mrb_bool sp_FloatArray_intersect_p(sp_FloatArray*a,sp_FloatArray*b){if(!a||!b)return 0;for(mrb_int i=0;i<a->len;i++)if(sp_FloatArray_include(b,a->data[i]))return 1;return 0;}
sp_FloatArray*sp_FloatArray_union(sp_FloatArray*a,sp_FloatArray*b){sp_FloatArray*r=sp_FloatArray_new();if(a)for(mrb_int i=0;i<a->len;i++){mrb_float v=a->data[i];if(!sp_FloatArray_include(r,v))sp_FloatArray_push(r,v);}if(b){for(mrb_int i=0;i<b->len;i++){mrb_float v=b->data[i];if(!sp_FloatArray_include(r,v))sp_FloatArray_push(r,v);}}return r;}
sp_FloatArray*sp_FloatArray_difference(sp_FloatArray*a,sp_FloatArray*b){sp_FloatArray*r=sp_FloatArray_new();for(mrb_int i=0;i<a->len;i++){mrb_float v=a->data[i];if(!sp_FloatArray_include(b,v))sp_FloatArray_push(r,v);}return r;}
sp_FloatArray*sp_FloatArray_uniq(sp_FloatArray*a){SP_GC_ROOT(a);sp_FloatArray*b=sp_FloatArray_new();if(!a)return b;for(mrb_int i=0;i<a->len;i++){mrb_float v=sp_FloatArray_get(a,i);if(!sp_FloatArray_include(b,v))sp_FloatArray_push(b,v);}return b;}

/* ============================= sp_PtrArray ============================ */
/* `Array#delete_at(i)` -- remove and return the element at index i.
   Negative indices count from the end; NULL when out of range. */
void*sp_PtrArray_delete_at(sp_PtrArray*a,mrb_int i){if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;void*v=a->data[i];for(mrb_int j=i;j<a->len-1;j++)a->data[j]=a->data[j+1];a->len--;return v;}
void sp_PtrArray_reverse_bang(sp_PtrArray*a){for(mrb_int i=0,j=a->len-1;i<j;i++,j--){void*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
void sp_PtrArray_rotate_bang(sp_PtrArray*a,mrb_int n){if(a->len<=0)return;n=((n%a->len)+a->len)%a->len;if(n==0)return;void**d=a->data;mrb_int lo=0,hi=n-1;while(lo<hi){void*t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=n;hi=a->len-1;while(lo<hi){void*t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=0;hi=a->len-1;while(lo<hi){void*t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}}
sp_PtrArray*sp_PtrArray_dup(sp_PtrArray*a){sp_PtrArray*b=sp_PtrArray_new_scan(a->scan_elem);for(mrb_int i=0;i<a->len;i++)sp_PtrArray_push(b,a->data[i]);return b;}
sp_PtrArray*sp_PtrArray_slice(sp_PtrArray*a,mrb_int start,mrb_int len){if(start<0)start+=a->len;if(start<0)start=0;sp_PtrArray*b=sp_PtrArray_new_scan(a->scan_elem);if(start>=a->len||len<=0)return b;if(start+len>a->len)len=a->len-start;for(mrb_int i=0;i<len;i++)sp_PtrArray_push(b,a->data[start+i]);return b;}
void sp_PtrArray_shuffle_bang(sp_PtrArray*a){for(mrb_int i=a->len-1;i>0;i--){mrb_int j=sp_krand_below(i+1);void*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
sp_PtrArray*sp_PtrArray_shuffle(sp_PtrArray*a){sp_PtrArray*b=sp_PtrArray_dup(a);sp_PtrArray_shuffle_bang(b);return b;}
void *sp_PtrArray_sample(sp_PtrArray*a){if(a->len<=0)return NULL;return a->data[sp_krand_below(a->len)];}

/* ============================= sp_StrArray ============================ */
void sp_StrArray_replace(sp_StrArray*dst,sp_StrArray*src){dst->len=0;if(src->len>dst->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)dst-sizeof(sp_gc_hdr));void*nd;if(dst->data==dst->inline_data){nd=malloc(sizeof(const char*)*src->len);if(!nd){perror("malloc");exit(1);}}else{SP_GC_CTR_SUB(sp_gc_bytes,sizeof(const char*)*dst->cap);h->size-=sizeof(const char*)*dst->cap;nd=realloc(dst->data,sizeof(const char*)*src->len);if(!nd){perror("realloc");exit(1);}}dst->data=(const char**)nd;dst->cap=src->len;h->size+=sizeof(const char*)*dst->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(const char*)*dst->cap);}memcpy(dst->data,src->data,sizeof(const char*)*src->len);dst->len=src->len;}
const char*sp_StrArray_pop(sp_StrArray*a){if(!a||a->len<=0)return NULL;if(a->frozen){sp_raise_frozen_array();return NULL;}return a->data[--a->len];}
const char*sp_StrArray_shift(sp_StrArray*a){if(!a||a->len<=0)return NULL;if(a->frozen){sp_raise_frozen_array();return NULL;}const char*v=a->data[0];memmove(a->data,a->data+1,(size_t)(--a->len)*sizeof(const char*));return v;}
/* a[start, len] / a[start..end] for StrArray. Same negative-start and
   length-clamping semantics as sp_IntArray_slice. */
sp_StrArray*sp_StrArray_slice(sp_StrArray*a,mrb_int start,mrb_int len){SP_GC_ROOT(a);if(start<0)start+=a->len;if(start<0)start=0;sp_StrArray*b=sp_StrArray_new();if(start>=a->len||len<=0)return b;if(start+len>a->len)len=a->len-start;for(mrb_int i=0;i<len;i++)sp_StrArray_push(b,a->data[start+i]);return b;}
sp_StrArray*sp_StrArray_slice_range(sp_StrArray*a,mrb_int start,mrb_int end_,mrb_int excl){if(end_<0)end_+=a->len;if(start<0)start+=a->len;mrb_int n=end_-start+(excl?0:1);if(n<0||start<0)n=0;return sp_StrArray_slice(a,start,n);}
void sp_StrArray_reverse_bang(sp_StrArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}for(mrb_int i=0,j=a->len-1;i<j;i++,j--){const char*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
void sp_StrArray_rotate_bang(sp_StrArray*a,mrb_int n){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}if(a->len<=0)return;n=((n%a->len)+a->len)%a->len;if(n==0)return;const char**d=a->data;mrb_int lo=0,hi=n-1;while(lo<hi){const char*t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=n;hi=a->len-1;while(lo<hi){const char*t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}lo=0;hi=a->len-1;while(lo<hi){const char*t=d[lo];d[lo]=d[hi];d[hi]=t;lo++;hi--;}}
static int _sp_str_cmp(const void*a,const void*b){return strcmp(*(const char*const*)a,*(const char*const*)b);}
void sp_StrArray_sort_bang(sp_StrArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}qsort(a->data,a->len,sizeof(const char*),_sp_str_cmp);}
void sp_StrArray_uniq_bang(sp_StrArray*a){if(!a||a->frozen){if(a&&a->frozen)sp_raise_frozen_array();return;}for(mrb_int i=0;i<a->len;){int dup=0;for(mrb_int j=0;j<i;j++){if(a->data[j]==a->data[i]||(a->data[j]&&a->data[i]&&!strcmp(a->data[j],a->data[i]))){dup=1;break;}}if(dup){for(mrb_int k2=i;k2<a->len-1;k2++)a->data[k2]=a->data[k2+1];a->len--;}else i++;}}
sp_StrArray*sp_StrArray_uniq(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*b=sp_StrArray_new();if(!a)return b;for(mrb_int i=0;i<a->len;i++){int found=0;for(mrb_int j=0;j<b->len;j++){if(b->data[j]==a->data[i]||(b->data[j]&&a->data[i]&&!strcmp(b->data[j],a->data[i]))){found=1;break;}}if(!found)sp_StrArray_push(b,a->data[i]);}return b;}
const char*sp_StrArray_join(sp_StrArray*a,const char*sep){size_t sl=strlen(sep),cap=256;char*buf=(char*)malloc(cap);size_t len=0;for(mrb_int i=0;i<a->len;i++){if(i>0){if(len+sl>=cap){cap*=2;buf=(char*)realloc(buf,cap);}memcpy(buf+len,sep,sl);len+=sl;}const char*_e=a->data[i]?a->data[i]:"";size_t el=strlen(_e);if(len+el>=cap){cap=((len+el)*2)+1;buf=(char*)realloc(buf,cap);}memcpy(buf+len,_e,el);len+=el;}buf[len]=0;char*r=sp_str_alloc(len);memcpy(r,buf,len);free(buf);return r;}
mrb_bool sp_StrArray_include(sp_StrArray*a,const char*v){if(!a)return FALSE;for(mrb_int i=0;i<a->len;i++)if(strcmp(a->data[i],v)==0)return TRUE;return FALSE;}
sp_StrArray*sp_StrArray_intersect(sp_StrArray*a,sp_StrArray*b){sp_StrArray*r=sp_StrArray_new();if(!a||!b)return r;for(mrb_int i=0;i<a->len;i++){const char*v=a->data[i];if(sp_StrArray_include(b,v)&&!sp_StrArray_include(r,v))sp_StrArray_push(r,v);}return r;}
mrb_bool sp_StrArray_intersect_p(sp_StrArray*a,sp_StrArray*b){if(!a||!b)return 0;for(mrb_int i=0;i<a->len;i++)if(sp_StrArray_include(b,a->data[i]))return 1;return 0;}
sp_StrArray*sp_StrArray_union(sp_StrArray*a,sp_StrArray*b){sp_StrArray*r=sp_StrArray_new();if(a)for(mrb_int i=0;i<a->len;i++){const char*v=a->data[i];if(!sp_StrArray_include(r,v))sp_StrArray_push(r,v);}if(b){for(mrb_int i=0;i<b->len;i++){const char*v=b->data[i];if(!sp_StrArray_include(r,v))sp_StrArray_push(r,v);}}return r;}
sp_StrArray*sp_StrArray_difference(sp_StrArray*a,sp_StrArray*b){sp_StrArray*r=sp_StrArray_new();for(mrb_int i=0;i<a->len;i++){const char*v=a->data[i];if(!sp_StrArray_include(b,v))sp_StrArray_push(r,v);}return r;}
/* min/max by String#<=> (byte comparison via strcmp); NULL (nil) when empty.
   nil (NULL) elements are skipped so a holey/sparse array can't crash strcmp. */
const char*sp_StrArray_min(sp_StrArray*a){if(!a||a->len<=0)return NULL;const char*m=NULL;for(mrb_int i=0;i<a->len;i++){const char*x=a->data[i];if(x&&(!m||strcmp(x,m)<0))m=x;}return m;}
const char*sp_StrArray_max(sp_StrArray*a){if(!a||a->len<=0)return NULL;const char*m=NULL;for(mrb_int i=0;i<a->len;i++){const char*x=a->data[i];if(x&&(!m||strcmp(x,m)>0))m=x;}return m;}
mrb_int sp_StrArray_index(sp_StrArray*a,const char*v){for(mrb_int i=0;i<a->len;i++)if(strcmp(a->data[i],v)==0)return i;return -1;}
mrb_int sp_StrArray_rindex(sp_StrArray*a,const char*v){for(mrb_int i=a->len-1;i>=0;i--)if(strcmp(a->data[i],v)==0)return i;return -1;}
sp_StrArray*sp_StrArray_compact(sp_StrArray*a){sp_StrArray*r=sp_StrArray_new();for(mrb_int i=0;i<a->len;i++)if(a->data[i]!=NULL)sp_StrArray_push(r,a->data[i]);return r;}
const char*sp_StrArray_delete_at(sp_StrArray*a,mrb_int i){if(!a)return NULL;if(a->frozen){sp_raise_frozen_array();return NULL;}if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;const char*v=a->data[i];for(mrb_int j=i;j<a->len-1;j++)a->data[j]=a->data[j+1];a->len--;return v;}
const char*sp_StrArray_delete(sp_StrArray*a,const char*v){if(!a)return NULL;if(a->frozen){sp_raise_frozen_array();return NULL;}mrb_int w=0;const char*found=NULL;for(mrb_int i=0;i<a->len;i++){if(strcmp(a->data[i],v)!=0){a->data[w]=a->data[i];w++;}else{found=a->data[i];}}a->len=w;return found;}
void sp_StrArray_insert(sp_StrArray*a,mrb_int i,const char*v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}mrb_int orig=i;if(i<0)i+=a->len+1;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)(-(a->len+1))));while(i>a->len)sp_StrArray_push(a,NULL);/* CRuby pads with nils past the end */sp_StrArray_push(a,sp_str_empty);for(mrb_int j=a->len-1;j>i;j--)a->data[j]=a->data[j-1];a->data[i]=v;}
void sp_StrArray_shuffle_bang(sp_StrArray*a){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}for(mrb_int i=a->len-1;i>0;i--){mrb_int j=sp_krand_below(i+1);const char*t=a->data[i];a->data[i]=a->data[j];a->data[j]=t;}}
sp_StrArray*sp_StrArray_dup(sp_StrArray*a){SP_GC_ROOT(a);sp_StrArray*r=sp_StrArray_new();sp_StrArray_replace(r,a);return r;}
sp_StrArray*sp_StrArray_sort(sp_StrArray*a){sp_StrArray*b=sp_StrArray_dup(a);sp_StrArray_sort_bang(b);return b;}
sp_StrArray*sp_StrArray_shuffle(sp_StrArray*a){sp_StrArray*r=sp_StrArray_new();sp_StrArray_replace(r,a);sp_StrArray_shuffle_bang(r);return r;}
const char *sp_StrArray_sample(sp_StrArray*a){if(a->len<=0)return sp_str_empty;return a->data[sp_krand_below(a->len)];}

/* ============ poly/inspect-dependent array ops (display, concat, to_poly) ============ */
sp_StrArray *sp_StrArray_from_string_range(const char *s, const char *e, mrb_int excl) {
  sp_StrArray *a = sp_StrArray_new();
  if (!s || !e) return a;
  const char *cur = s;
  int iters = 0;
  while (iters < 4096) {
    int cmp = strcmp(cur, e);
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
const char*sp_IntArray_inspect(sp_IntArray*a){return a?sp_inspect_container(sp_box_obj(a,SP_BUILTIN_INT_ARRAY)):"nil";}
const char*sp_FloatArray_inspect(sp_FloatArray*a){return a?sp_inspect_container(sp_box_obj(a,SP_BUILTIN_FLT_ARRAY)):"nil";}
const char*sp_FloatArray_join(sp_FloatArray*a,const char*sep){size_t sl=strlen(sep),cap=256;char*buf=(char*)malloc(cap);size_t len=0;if(a){for(mrb_int i=0;i<a->len;i++){if(i>0){if(len+sl>=cap){cap*=2;buf=(char*)realloc(buf,cap);}memcpy(buf+len,sep,sl);len+=sl;}const char*es=sp_float_to_s(a->data[i]);size_t el=strlen(es);if(len+el>=cap){while(len+el>=cap)cap*=2;buf=(char*)realloc(buf,cap);}memcpy(buf+len,es,el);len+=el;}}buf[len]=0;char*r=sp_str_alloc(len);memcpy(r,buf,len);free(buf);return r;}
mrb_bool sp_FloatArray_eq(sp_FloatArray*a,sp_FloatArray*b){if(!a||!b)return a==b;if(a->len!=b->len)return FALSE;for(mrb_int i=0;i<a->len;i++)if(a->data[i]!=b->data[i])return FALSE;return TRUE;}
const char*sp_StrArray_inspect(sp_StrArray*a){return a?sp_inspect_container(sp_box_obj(a,SP_BUILTIN_STR_ARRAY)):"nil";}
const char*sp_PtrArray_inspect(sp_PtrArray*a){if(!a)return "nil";SP_GC_ROOT(a);sp_String*s=sp_String_new("[");SP_GC_ROOT(s);for(mrb_int i=0;i<a->len;i++){if(i>0)sp_String_append(s,", ");sp_String_append(s,"#<Object>");}sp_String_append(s,"]");return s->data;}
/* Array#slice_before(delim): start a new chunk before each element == delim. */
sp_PtrArray*sp_IntArray_slice_before(sp_IntArray*a,mrb_int d){SP_GC_ROOT(a);sp_PtrArray*out=sp_PtrArray_new();SP_GC_ROOT(out);if(!a)return out;sp_IntArray*cur=sp_IntArray_new();SP_GC_ROOT(cur);for(mrb_int i=0;i<a->len;i++){mrb_int e=a->data[a->start+i];if(e==d&&cur->len>0){sp_PtrArray_push(out,cur);cur=sp_IntArray_new();}sp_IntArray_push(cur,e);}if(cur->len>0)sp_PtrArray_push(out,cur);return out;}
/* Array#slice_after(delim): end a chunk after each element == delim. */
sp_PtrArray*sp_IntArray_slice_after(sp_IntArray*a,mrb_int d){SP_GC_ROOT(a);sp_PtrArray*out=sp_PtrArray_new();SP_GC_ROOT(out);if(!a)return out;sp_IntArray*cur=sp_IntArray_new();SP_GC_ROOT(cur);for(mrb_int i=0;i<a->len;i++){mrb_int e=a->data[a->start+i];sp_IntArray_push(cur,e);if(e==d){sp_PtrArray_push(out,cur);cur=sp_IntArray_new();}}if(cur->len>0)sp_PtrArray_push(out,cur);return out;}
sp_PtrArray *sp_IntArray_product(sp_IntArray *a, sp_IntArray *b) {
  SP_GC_ROOT(a); SP_GC_ROOT(b);
  sp_PtrArray *out = sp_PtrArray_new();
  SP_GC_ROOT(out);
  if (!a || !b) return out;
  for (mrb_int i = 0; i < a->len; i++) {
    for (mrb_int j = 0; j < b->len; j++) {
      sp_IntArray *pair = sp_IntArray_new();
      sp_IntArray_push(pair, a->data[a->start + i]);
      sp_IntArray_push(pair, b->data[b->start + j]);
      sp_PtrArray_push(out, pair);
    }
  }
  return out;
}
const char*sp_PtrArray_str_join(sp_PtrArray*a,const char*sep){mrb_int al=a->len;if(al==0)return sp_str_empty;size_t sl=strlen(sep),total=0;for(mrb_int i=0;i<al;i++){if(i>0)total+=sl;sp_String*s=(sp_String*)a->data[i];if(s)total+=(size_t)s->len;}char*r=sp_str_alloc(total);size_t cur=0;for(mrb_int i=0;i<al;i++){if(i>0){memcpy(r+cur,sep,sl);cur+=sl;}sp_String*s=(sp_String*)a->data[i];if(s&&s->len){memcpy(r+cur,s->data,(size_t)s->len);cur+=(size_t)s->len;}}return r;}
sp_RbVal sp_IntArray_index_poly(sp_IntArray *a, mrb_int v)         { mrb_int n = sp_IntArray_index(a, v);   return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_IntArray_rindex_poly(sp_IntArray *a, mrb_int v)        { mrb_int n = sp_IntArray_rindex(a, v);  return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_StrArray_index_poly(sp_StrArray *a, const char *v)     { mrb_int n = sp_StrArray_index(a, v);   return n < 0 ? sp_box_nil() : sp_box_int(n); }
sp_RbVal sp_StrArray_rindex_poly(sp_StrArray *a, const char *v)    { mrb_int n = sp_StrArray_rindex(a, v);  return n < 0 ? sp_box_nil() : sp_box_int(n); }
mrb_int sp_IntArray_index_opt(sp_IntArray *a, mrb_int v)           { mrb_int n = sp_IntArray_index(a, v);   return n < 0 ? SP_INT_NIL : n; }
mrb_int sp_IntArray_rindex_opt(sp_IntArray *a, mrb_int v)          { mrb_int n = sp_IntArray_rindex(a, v);  return n < 0 ? SP_INT_NIL : n; }
const int64_t *sp_IntArray_ffi_data(sp_IntArray *a) { return a ? (const int64_t *)(a->data + a->start) : (const int64_t *)0; }
const double  *sp_FloatArray_ffi_data(sp_FloatArray *a) { return a ? (const double *)a->data : (const double *)0; }
sp_IntArray *sp_IntArray_concat(sp_IntArray *a, sp_IntArray *b) { sp_IntArray *r = sp_IntArray_new(); SP_GC_ROOT(r); if (a) for (mrb_int i = 0; i < a->len; i++) sp_IntArray_push(r, sp_IntArray_get(a, i)); if (b) for (mrb_int i = 0; i < b->len; i++) sp_IntArray_push(r, sp_IntArray_get(b, i)); return r; }
sp_StrArray *sp_StrArray_concat(sp_StrArray *a, sp_StrArray *b) { sp_StrArray *r = sp_StrArray_new(); SP_GC_ROOT(r); if (a) for (mrb_int i = 0; i < a->len; i++) sp_StrArray_push(r, sp_StrArray_get(a, i)); if (b) for (mrb_int i = 0; i < b->len; i++) sp_StrArray_push(r, sp_StrArray_get(b, i)); return r; }
sp_FloatArray *sp_FloatArray_concat(sp_FloatArray *a, sp_FloatArray *b) { sp_FloatArray *r = sp_FloatArray_new(); SP_GC_ROOT(r); if (a) for (mrb_int i = 0; i < a->len; i++) sp_FloatArray_push(r, sp_FloatArray_get(a, i)); if (b) for (mrb_int i = 0; i < b->len; i++) sp_FloatArray_push(r, sp_FloatArray_get(b, i)); return r; }
sp_PolyArray *sp_IntArray_to_poly(sp_IntArray *a) {
  SP_GC_ROOT(a);
  sp_PolyArray *r = sp_PolyArray_new();
  SP_GC_ROOT(r);
  if (!a) return r;
  for (mrb_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_box_int(a->data[a->start + i]));
  return r;
}
sp_PolyArray *sp_StrArray_to_poly_fmt(sp_StrArray *a) {
  sp_PolyArray *r = sp_PolyArray_new();
  if (!a) return r;
  for (mrb_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_box_str(a->data[i]));
  return r;
}
sp_PolyArray *sp_FloatArray_to_poly(sp_FloatArray *a) {
  SP_GC_ROOT(a);
  sp_PolyArray *r = sp_PolyArray_new();
  SP_GC_ROOT(r);
  if (!a) return r;
  for (mrb_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_box_float(a->data[i]));
  return r;
}
sp_IntArray *sp_IntArray_slice_bang(sp_IntArray *a, mrb_int from, mrb_int n) {
  if (!a) return sp_IntArray_new();
  if (a->frozen) { sp_raise_frozen_array(); return sp_IntArray_new(); }
  if (from < 0) from += a->len;
  if (from < 0) from = 0;
  if (from > a->len) from = a->len;
  if (n < 0) n = 0;
  if (from + n > a->len) n = a->len - from;
  sp_IntArray *r = sp_IntArray_new();
  for (mrb_int i = 0; i < n; i++) sp_IntArray_push(r, a->data[a->start + from + i]);
  if (from == 0) {
    a->start += n;
    a->len -= n;
  }
else {
    for (mrb_int i = from; i + n < a->len; i++) a->data[a->start + i] = a->data[a->start + i + n];
    a->len -= n;
  }
  return r;
}
sp_FloatArray *sp_FloatArray_slice_bang(sp_FloatArray *a, mrb_int from, mrb_int n) {
  if (!a) return sp_FloatArray_new();
  if (a->frozen) { sp_raise_frozen_array(); return sp_FloatArray_new(); }
  if (from < 0) from += a->len;
  if (from < 0) from = 0;
  if (from > a->len) from = a->len;
  if (n < 0) n = 0;
  if (from + n > a->len) n = a->len - from;
  sp_FloatArray *r = sp_FloatArray_new();
  for (mrb_int i = 0; i < n; i++) sp_FloatArray_push(r, a->data[from + i]);
  for (mrb_int i = from; i + n < a->len; i++) a->data[i] = a->data[i + n];
  a->len -= n;
  return r;
}
sp_StrArray *sp_StrArray_slice_bang(sp_StrArray *a, mrb_int from, mrb_int n) {
  if (!a) return sp_StrArray_new();
  if (a->frozen) { sp_raise_frozen_array(); return sp_StrArray_new(); }
  if (from < 0) from += a->len;
  if (from < 0) from = 0;
  if (from > a->len) from = a->len;
  if (n < 0) n = 0;
  if (from + n > a->len) n = a->len - from;
  sp_StrArray *r = sp_StrArray_new();
  for (mrb_int i = 0; i < n; i++) sp_StrArray_push(r, a->data[from + i]);
  for (mrb_int i = from; i + n < a->len; i++) a->data[i] = a->data[i + n];
  a->len -= n;
  return r;
}
sp_PtrArray *sp_PtrArray_slice_bang(sp_PtrArray *a, mrb_int from, mrb_int n) {
  if (!a) return sp_PtrArray_new_scan(NULL);
  if (a->frozen) { sp_raise_frozen_array(); return sp_PtrArray_new_scan(a->scan_elem); }
  if (from < 0) from += a->len;
  if (from < 0) from = 0;
  if (from > a->len) from = a->len;
  if (n < 0) n = 0;
  if (from + n > a->len) n = a->len - from;
  sp_PtrArray *r = sp_PtrArray_new_scan(a->scan_elem);
  for (mrb_int i = 0; i < n; i++) sp_PtrArray_push(r, a->data[from + i]);
  for (mrb_int i = from; i + n < a->len; i++) a->data[i] = a->data[i + n];
  a->len -= n;
  return r;
}

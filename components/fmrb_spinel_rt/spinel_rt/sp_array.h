#ifndef SP_ARRAY_H
#define SP_ARRAY_H
/* sp_array.h -- typed array hot core + cold-op surface.
 *
 * The struct layouts (sp_IntArray, ...) live in sp_types.h. The hot
 * accessors (new / push / pop / shift / get / set / length / empty) stay
 * inline here so every generated TU compiles them identically --
 * relocating them out of spinel_rt.h into this shared header is a pure
 * textual move with no codegen change. The cold ops (sort / slice / dup /
 * set algebra / join / ...) are compiled once into libspinel_rt.a
 * (lib/sp_array.c); this header only declares them.
 *
 * sp_sprintf / sp_raise_cls / sp_raise_frozen_array are provided by the
 * generated TU and resolved at the final link, the same way lib/sp_core.c
 * calls them -- so lib/sp_array.c can use them without a runtime include.
 */
#include <math.h>      /* isnan / isinf / signbit / NAN / fabs for sp_float_sum_step */
#include "sp_gc.h"      /* sp_gc_hdr, sp_gc_bytes, SP_GC_ROOT, sp_oom_die */
#include "sp_alloc.h"   /* sp_gc_alloc, sp_str_alloc, sp_raise_cls, sp_raise_frozen_array */

#ifndef SP_MULTI_CTX  /* T4-0: per-ctx macro under SP_MULTI_CTX */
const char *sp_sprintf(const char *fmt, ...);  /* defined in the generated TU */
#endif

/* ============================ sp_IntArray ============================ */
/* `frozen` rides in the struct (not the GC header) so the hot push /
   []= paths read it from the same cache line as len/cap -- no extra
   cache miss vs. the GC-header bit. calloc in sp_gc_alloc zero-inits
   it, so constructors need no change. Issue #918. */
static void sp_IntArray_fin(void*p){sp_pl_free(((sp_IntArray*)p)->data);}
static sp_IntArray*sp_IntArray_new(void){sp_IntArray*a=(sp_IntArray*)sp_gc_alloc(sizeof(sp_IntArray),sp_IntArray_fin,NULL);a->cap=16;a->data=(sp_int*)sp_pl_alloc(sizeof(sp_int)*a->cap);if(!a->data)sp_oom_die();a->start=0;a->len=0;{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));h->size+=sizeof(sp_int)*a->cap;sp_gc_bytes_add(sizeof(sp_int)*a->cap);}return a;}
static void __attribute__((noinline)) sp_IntArray_push_grow(sp_IntArray*a){if(a->start>0){memmove(a->data,a->data+a->start,sizeof(sp_int)*a->len);a->start=0;if(a->len<a->cap)return;}{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*a->cap);h->size-=sizeof(sp_int)*a->cap;a->cap=((((((a->cap*2))))))+1;void*nd=sp_pl_realloc(a->data,sizeof(sp_int)*a->cap);if(!nd)sp_oom_die();a->data=(sp_int*)nd;h->size+=sizeof(sp_int)*a->cap;sp_gc_bytes_add(sizeof(sp_int)*a->cap);}}
static inline void sp_IntArray_push(sp_IntArray*a,sp_int v){if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}if(a->start+a->len>=a->cap)sp_IntArray_push_grow(a);a->data[a->start+a->len]=v;a->len++;}
/* Issue #826/#832: empty pop/shift return SP_INT_NIL (nullable int
   sentinel) to match MRI's nil; callers treat as int?. Without the
   guard, `--a->len` wraps to -1 and reads past the buffer start. */
static inline sp_int sp_IntArray_pop(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return SP_INT_NIL;}return a->data[a->start+--a->len];}
static inline sp_int sp_IntArray_shift(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return SP_INT_NIL;}sp_int v=a->data[a->start];a->start++;a->len--;return v;}
static inline sp_int sp_IntArray_length(sp_IntArray*a){return a->len;}
static inline sp_bool sp_IntArray_empty(sp_IntArray*a){return a->len==0;}
static inline sp_int sp_IntArray_get(sp_IntArray*a,sp_int i){if(!a)return SP_INT_NIL;if(i<0)i+=a->len;if(i<0||i>=a->len)return SP_INT_NIL;return a->data[a->start+i];}
/* Issue #769: a very-negative i leaves i negative after the `i += a->len`
   adjustment. CRuby raises IndexError; spinel no-ops as the safest
   fallback (raising from a typed-array set would need setjmp plumbing
   throughout the call chain). */
static void sp_IntArray_set_slow(sp_IntArray*a,sp_int i,sp_int v){if(i<0)return;while(a->start+i>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_int)*a->cap);h->size-=sizeof(sp_int)*a->cap;a->cap=((((((a->cap*2))))))+1;a->data=(sp_int*)sp_pl_realloc(a->data,sizeof(sp_int)*a->cap);h->size+=sizeof(sp_int)*a->cap;sp_gc_bytes_add(sizeof(sp_int)*a->cap);}while(i>=a->len){a->data[a->start+a->len]=SP_INT_NIL;a->len++;}  /* gap slots read as nil */a->data[a->start+i]=v;}
/* Issue #839: an extreme negative index (still negative after `i += len`)
   raises IndexError per MRI. */
static void __attribute__((noinline, cold)) sp_IntArray_set_cold(sp_IntArray*a,sp_int i,sp_int v){if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_INT_ARRAY);return;}sp_int orig=i;if(i<0)i+=a->len;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));if(i<a->len){a->data[a->start+i]=v;return;}sp_IntArray_set_slow(a,i,v);}
static inline void sp_IntArray_set(sp_IntArray*a,sp_int i,sp_int v){if(SP_LIKELY(a&&!a->frozen&&i>=0&&i<a->len)){a->data[a->start+i]=v;return;}sp_IntArray_set_cold(a,i,v);}

/* ---- sp_IntArray cold ops (compiled in lib/sp_array.c) ---- */
sp_IntArray *sp_IntArray_from_range(sp_int s, sp_int e);
sp_IntArray *sp_IntArray_from_range_step(sp_int beg, sp_int end, sp_int step, sp_int excl);
sp_IntArray *sp_IntArray_dup(sp_IntArray *a);
sp_IntArray *sp_IntArray_slice(sp_IntArray *a, sp_int start, sp_int len);
sp_IntArray *sp_IntArray_slice_range(sp_IntArray *a, sp_int start, sp_int end_, sp_int excl);
void sp_IntArray_replace(sp_IntArray *dst, sp_IntArray *src);
void sp_IntArray_splice(sp_IntArray *a, sp_int start, sp_int len, const sp_int *src, sp_int srcn);
void sp_FloatArray_splice(sp_FloatArray *a, sp_int start, sp_int len, const sp_float *src, sp_int srcn);
void sp_StrArray_splice(sp_StrArray *a, sp_int start, sp_int len, const char *const *src, sp_int srcn);
void sp_PolyArray_splice(sp_PolyArray *a, sp_int start, sp_int len, sp_RbVal src);
void sp_IntArray_reverse_bang(sp_IntArray *a);
void sp_IntArray_rotate_bang(sp_IntArray *a, sp_int n);
sp_IntArray *sp_IntArray_sort(sp_IntArray *a);
void sp_IntArray_sort_bang(sp_IntArray *a);
void sp_IntArray_uniq_bang(sp_IntArray *a);
void sp_IntArray_shuffle_bang(sp_IntArray *a);
sp_IntArray *sp_IntArray_shuffle(sp_IntArray *a);
sp_int sp_IntArray_sample(sp_IntArray *a);
sp_int sp_IntArray_min(sp_IntArray *a);
sp_int sp_IntArray_max(sp_IntArray *a);
const char *sp_StrArray_min(sp_StrArray *a);
const char *sp_StrArray_max(sp_StrArray *a);
sp_int sp_IntArray_sum(sp_IntArray *a, sp_int init);
sp_bool sp_IntArray_include(sp_IntArray *a, sp_int v);
sp_int sp_IntArray_index(sp_IntArray *a, sp_int v);
sp_int sp_IntArray_rindex(sp_IntArray *a, sp_int v);
sp_int sp_IntArray_delete_at(sp_IntArray *a, sp_int i);
sp_int sp_IntArray_delete(sp_IntArray *a, sp_int v);
void sp_IntArray_insert(sp_IntArray *a, sp_int i, sp_int v);
sp_IntArray *sp_IntArray_uniq(sp_IntArray *a);
sp_IntArray *sp_IntArray_intersect(sp_IntArray *a, sp_IntArray *b);
sp_bool sp_IntArray_intersect_p(sp_IntArray *a, sp_IntArray *b);
sp_IntArray *sp_IntArray_union(sp_IntArray *a, sp_IntArray *b);
sp_IntArray *sp_IntArray_difference(sp_IntArray *a, sp_IntArray *b);
void sp_IntArray_unshift(sp_IntArray *a, sp_int v);
const char *sp_IntArray_join(sp_IntArray *a, const char *sep);
sp_bool sp_IntArray_eq(sp_IntArray *a, sp_IntArray *b);
sp_int sp_IntArray_cmp(sp_IntArray *a, sp_IntArray *b);

/* =========================== sp_FloatArray =========================== */
static void sp_FloatArray_fin(void*p){sp_FloatArray*a=(sp_FloatArray*)p;sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_float)*a->cap);h->size-=sizeof(sp_float)*a->cap;sp_pl_free(a->data);}
static sp_FloatArray*sp_FloatArray_new(void){sp_FloatArray*a=(sp_FloatArray*)sp_gc_alloc(sizeof(sp_FloatArray),sp_FloatArray_fin,NULL);a->cap=16;a->data=(sp_float*)sp_pl_alloc(sizeof(sp_float)*a->cap);if(!a->data)sp_oom_die();a->len=0;{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));h->size+=sizeof(sp_float)*a->cap;sp_gc_bytes_add(sizeof(sp_float)*a->cap);}return a;}
static inline void sp_FloatArray_push(sp_FloatArray*a,sp_float v){if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}if(a->len>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(sp_float)*a->cap);h->size-=sizeof(sp_float)*a->cap;a->cap=((((((a->cap*2))))))+1;a->data=(sp_float*)sp_pl_realloc(a->data,sizeof(sp_float)*a->cap);h->size+=sizeof(sp_float)*a->cap;sp_gc_bytes_add(sizeof(sp_float)*a->cap);}a->data[a->len++]=v;}
/* CRuby answers nil for pop/shift on an empty array; the int side already
   returns SP_INT_NIL and the float side answered 0.0, so a drained float
   array read back as a real zero (#4288). sp_float_nil() is the float slot's
   own sentinel, which sp_float_opt_inspect and the boxing helpers know. */
static inline sp_float sp_FloatArray_pop(sp_FloatArray*a){if(!a||a->len<=0)return sp_float_nil();if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return sp_float_nil();}return a->data[--a->len];}
static inline sp_float sp_FloatArray_shift(sp_FloatArray*a){if(!a||a->len==0)return sp_float_nil();if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return sp_float_nil();}sp_float v=a->data[0];for(sp_int i=0;i+1<a->len;i++)a->data[i]=a->data[i+1];a->len--;return v;}
/* FloatArray is 0-based (no `start` offset, unlike IntArray). delete_at
   returns 0.0 on out-of-range (delete_at's nil there). */
static inline sp_float sp_FloatArray_delete_at(sp_FloatArray*a,sp_int i){if(!a)return 0.0;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return 0.0;}if(i<0)i+=a->len;if(i<0||i>=a->len)return 0.0;sp_float v=a->data[i];for(sp_int j=i;j+1<a->len;j++)a->data[j]=a->data[j+1];a->len--;return v;}
static inline sp_int sp_FloatArray_length(sp_FloatArray*a){return a->len;}
static inline sp_bool sp_FloatArray_empty(sp_FloatArray*a){return a->len==0;}
static inline sp_float sp_FloatArray_get(sp_FloatArray*a,sp_int i){if(!a)return sp_float_nil();if(i<0)i+=a->len;if(i<0||i>=a->len)return sp_float_nil();return a->data[i];}
/* first/last as float? : nil (sentinel) when empty, else the element.
   `[i]` stays non-nullable (0.0 for OOB) -- only first/last produce nil. */
static inline sp_float sp_FloatArray_first_opt(sp_FloatArray*a){return (!a||a->len<=0)?sp_float_nil():sp_FloatArray_get(a,0);}
static inline sp_float sp_FloatArray_last_opt(sp_FloatArray*a){return (!a||a->len<=0)?sp_float_nil():sp_FloatArray_get(a,a->len-1);}
/* Issue #769: no-op for negative index after adjustment. */
static inline void sp_FloatArray_set(sp_FloatArray*a,sp_int i,sp_float v){if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_FLT_ARRAY);return;}sp_int orig=i;if(i<0)i+=a->len;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));while(i>=a->cap){a->cap=((((((a->cap*2))))))+1;a->data=(sp_float*)sp_pl_realloc(a->data,sizeof(sp_float)*a->cap);}while(i>=a->len){a->data[a->len]=sp_float_nil();a->len++;}a->data[i]=v;}  /* the gap is nil, not 0.0 (#3836) */

/* One step of CRuby's compensated summation (array.c ary_sum): Kahan-Babuska-
   Neumaier, where `comp` collects the low-order bits each add drops and is
   folded back once the run ends. The special-value arms are CRuby's own: a
   total that is already NaN stays NaN, a NaN element makes it NaN, and an
   Infinity element takes the total over unless it meets an Infinity of the
   other sign, which is NaN. Without them the compensation computed
   (inf - inf) and `[Float::INFINITY, 1.0].sum` answered NaN where Ruby
   answers Infinity. Shared so the typed float sum and the boxed fold in
   spinel_rt.h cannot drift apart. */
static inline void sp_float_sum_step(sp_float *sum, sp_float *comp, sp_float x) {
  sp_float f = *sum;
  if (isnan(f)) return;
  if (isnan(x)) { *sum = x; return; }
  if (isinf(x)) {
    *sum = (isinf(f) && signbit(x) != signbit(f)) ? (sp_float)NAN : x;
    return;
  }
  if (isinf(f)) return;
  {
    sp_float t = f + x;
    if (fabs(f) >= fabs(x)) *comp += (f - t) + x;
    else *comp += (x - t) + f;
    *sum = t;
  }
}

/* ---- sp_FloatArray cold ops (compiled in lib/sp_array.c) ---- */
void sp_FloatArray_unshift(sp_FloatArray *a, sp_float v);
sp_FloatArray *sp_FloatArray_from_step(sp_float beg, sp_float end, sp_float step, sp_int excl);
sp_float sp_FloatArray_min(sp_FloatArray *a);
sp_float sp_FloatArray_max(sp_FloatArray *a);
sp_float sp_FloatArray_sum(sp_FloatArray *a, sp_float init);
sp_float sp_FloatArray_sum_plain(sp_FloatArray *a, sp_float init);
void sp_FloatArray_replace(sp_FloatArray *dst, sp_FloatArray *src);
sp_FloatArray *sp_FloatArray_slice(sp_FloatArray *a, sp_int start, sp_int len);
sp_FloatArray *sp_FloatArray_slice_range(sp_FloatArray *a, sp_int start, sp_int end_, sp_int excl);
void sp_FloatArray_reverse_bang(sp_FloatArray *a);
void sp_FloatArray_rotate_bang(sp_FloatArray *a, sp_int n);
void sp_FloatArray_sort_bang(sp_FloatArray *a);
void sp_FloatArray_shuffle_bang(sp_FloatArray *a);
sp_FloatArray *sp_FloatArray_dup(sp_FloatArray *a);
sp_FloatArray *sp_FloatArray_sort(sp_FloatArray *a);
sp_FloatArray *sp_FloatArray_shuffle(sp_FloatArray *a);
sp_float sp_FloatArray_sample(sp_FloatArray *a);
sp_bool sp_FloatArray_include(sp_FloatArray *a, sp_float v);
sp_int sp_FloatArray_index(sp_FloatArray *a, sp_float v);
sp_int sp_FloatArray_rindex(sp_FloatArray *a, sp_float v);
sp_float sp_FloatArray_delete(sp_FloatArray *a, sp_float v);
sp_FloatArray *sp_FloatArray_intersect(sp_FloatArray *a, sp_FloatArray *b);
sp_bool sp_FloatArray_intersect_p(sp_FloatArray *a, sp_FloatArray *b);
sp_FloatArray *sp_FloatArray_union(sp_FloatArray *a, sp_FloatArray *b);
sp_FloatArray *sp_FloatArray_difference(sp_FloatArray *a, sp_FloatArray *b);
sp_FloatArray *sp_FloatArray_uniq(sp_FloatArray *a);

/* ============================= sp_PtrArray ============================ */
/* Array of void* pointers (user-class arrays, FFI pointer arrays). */
static void sp_PtrArray_fin(void*p){sp_PtrArray*a=(sp_PtrArray*)p;sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(void*)*a->cap);h->size-=sizeof(void*)*a->cap;sp_pl_free(a->data);}
static void sp_PtrArray_gc_scan(void*p){sp_PtrArray*a=(sp_PtrArray*)p;if(!a->scan_elem)return;for(sp_int i=0;i<a->len;i++){if(a->data[i])a->scan_elem(a->data[i]);}}
static sp_PtrArray*sp_PtrArray_new_scan(void(*scan_elem)(void*)){sp_PtrArray*a=(sp_PtrArray*)sp_gc_alloc(sizeof(sp_PtrArray),sp_PtrArray_fin,scan_elem?sp_PtrArray_gc_scan:NULL);a->cap=16;a->data=(void**)sp_pl_alloc(sizeof(void*)*a->cap);if(!a->data)sp_oom_die();a->len=0;a->scan_elem=scan_elem;{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));h->size+=sizeof(void*)*a->cap;sp_gc_bytes_add(sizeof(void*)*a->cap);}return a;}
static sp_PtrArray*sp_PtrArray_new(void){return sp_PtrArray_new_scan(sp_gc_mark);}
/* PtrArray for raw external pointers (FFI `:ptr` returns, dlopen handles).
   These don't carry sp_gc_hdr -- the default sp_gc_mark element scan would
   read undefined bytes and crash at collection. Skip per-element scanning;
   the array header itself is still GC-tracked. */
static sp_PtrArray*sp_PtrArray_new_noscan(void){return sp_PtrArray_new_scan(NULL);}
static inline void sp_PtrArray_push(sp_PtrArray*a,void*v){sp_gc_wb((void*)a); if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_PTR_ARRAY);return;}if(a->len>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(void*)*a->cap);h->size-=sizeof(void*)*a->cap;a->cap=((((((a->cap*2))))))+1;void*nd=sp_pl_realloc(a->data,sizeof(void*)*a->cap);if(!nd)sp_oom_die();a->data=(void**)nd;h->size+=sizeof(void*)*a->cap;sp_gc_bytes_add(sizeof(void*)*a->cap);}a->data[a->len++]=v;}
/* Array#pop on a `<X>_ptr_array`. Returns NULL when empty (matches CRuby's
   nil for typed-element arrays since the slot can't carry nil). #520. */
static inline void *sp_PtrArray_pop(sp_PtrArray*a){sp_gc_wb((void*)a); if(!a||a->len==0)return NULL;return a->data[--a->len];}
/* A pointer array boxed by reference (#4486): its elements are what the stamp
   says (sp_box_ptr_array_k), so an element read boxes a row as the typed
   array it is and an object as its own class, and a store takes exactly that
   kind or raises the TypeError the flat typed arrays raise. An array that was
   never stamped (no emitter boxes one without stamping) reads as opaque
   objects, which is what the erased id always meant. Shared by the runtime
   archive and the generated TU, so only sp_alloc.h/sp_gc.h names appear. */
static inline const char *sp_PtrArray_kind_name(sp_PtrArray *a) {
  switch (a->elem_kind) {
    case SP_PTR_ELEM_INT_ROWS: return "Array[Integer]";
    case SP_PTR_ELEM_FLT_ROWS: return "Array[Float]";
    case SP_PTR_ELEM_OBJ: return (a->elem_cls >= 0 && sp_obj_cls_name_fn) ? sp_obj_cls_name_fn(a->elem_cls) : "Object";
    default: return "Object";
  }
}
static inline sp_RbVal sp_PtrArray_elem_box(sp_PtrArray *a, void *e) {
  if (!e) return sp_box_nil();
  switch (a->elem_kind) {
    case SP_PTR_ELEM_INT_ROWS: return sp_box_obj(e, SP_BUILTIN_INT_ARRAY);
    case SP_PTR_ELEM_FLT_ROWS: return sp_box_obj(e, SP_BUILTIN_FLT_ARRAY);
    case SP_PTR_ELEM_OBJ: return sp_box_nullable_obj_dyn(e, 0);   /* the object's own class id, a subclass included */
    default: return sp_box_obj(e, SP_BUILTIN_OBJECT);
  }
}
static inline sp_RbVal sp_PtrArray_get_box(sp_PtrArray *a, sp_int i) {
  if (!a) return sp_box_nil();
  if (i < 0) i += a->len;
  if (i < 0 || i >= a->len) return sp_box_nil();
  return sp_PtrArray_elem_box(a, a->data[i]);
}
/* the class a boxed value would name in a TypeError, from what this header can see */
static inline const char *sp_PtrArray_val_class(sp_RbVal v) {
  switch (v.tag) {
    case SP_TAG_INT: return "Integer"; case SP_TAG_FLT: return "Float"; case SP_TAG_STR: return "String";
    case SP_TAG_NIL: return "NilClass"; case SP_TAG_SYM: return "Symbol";
    case SP_TAG_BOOL: return v.v.b ? "TrueClass" : "FalseClass";
    case SP_TAG_OBJ:
      if (v.cls_id >= 0) return sp_obj_cls_name_fn ? sp_obj_cls_name_fn(v.cls_id) : "Object";
      switch (v.cls_id) {
        case SP_BUILTIN_INT_ARRAY: case SP_BUILTIN_FLT_ARRAY: case SP_BUILTIN_STR_ARRAY:
        case SP_BUILTIN_SYM_ARRAY: case SP_BUILTIN_POLY_ARRAY: case SP_BUILTIN_PTR_ARRAY: return "Array";
        default: return "Object";
      }
    default: return "Object";
  }
}
/* can the array hold v? 1 with the pointer in *out (NULL for nil), else 0 */
static inline int sp_PtrArray_elem_ok(sp_PtrArray *a, sp_RbVal v, void **out) {
  *out = NULL;
  if (v.tag == SP_TAG_NIL) return 1;
  if (v.tag != SP_TAG_OBJ) return 0;
  switch (a->elem_kind) {
    case SP_PTR_ELEM_INT_ROWS: if (v.cls_id != SP_BUILTIN_INT_ARRAY) return 0; break;
    case SP_PTR_ELEM_FLT_ROWS: if (v.cls_id != SP_BUILTIN_FLT_ARRAY) return 0; break;
    case SP_PTR_ELEM_OBJ:
      if (!(a->elem_cls < 0 || v.cls_id == a->elem_cls ||
            (v.cls_id >= 0 && sp_class_le_id_fn && sp_class_le_id_fn(v.cls_id, a->elem_cls)))) return 0;
      break;
    default: break;
  }
  *out = v.v.p;
  return 1;
}
static inline void *sp_PtrArray_elem_unbox(sp_PtrArray *a, sp_RbVal v) {
  void *e;
  if (sp_PtrArray_elem_ok(a, v, &e)) return e;
  sp_exc_stage_recv(v);
  sp_raise_cls("TypeError", sp_sprintf("cannot store %s into an Array[%s]: a typed array holds one kind of element",
                                       sp_PtrArray_val_class(v), sp_PtrArray_kind_name(a)));
  return NULL;
}
/* the boxed elements of a pointer array, for the paths that work on a poly array */
static inline sp_PolyArray *sp_PtrArray_to_poly(sp_PtrArray *a) {
  SP_GC_ROOT(a);
  sp_PolyArray *r = sp_PolyArray_new(); SP_GC_ROOT(r);
  if (a) for (sp_int i = 0; i < a->len; i++) sp_PolyArray_push(r, sp_PtrArray_elem_box(a, a->data[i]));
  return r;
}
static inline void*sp_PtrArray_get(sp_PtrArray*a,sp_int i){if(!a)return NULL;if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;return a->data[i];}
/* Issue #770: bounds-check the final index; no-op out-of-range rather
   than writing into adjacent memory (typed slots have a fixed shape). */
static inline void sp_PtrArray_set(sp_PtrArray*a,sp_int i,void*v){sp_gc_wb((void*)a); if(!a)return;if(i<0)i+=a->len;if(i<0||i>=a->len)return;a->data[i]=v;}
/* `a[i] = v` through a boxed pointer array: Ruby's index rules (a negative
   index from the end, IndexError below -len, nil-fill past the end), where
   the typed setter above answers a fixed shape (#4486) */
static inline void sp_PtrArray_set_grow(sp_PtrArray*a,sp_int i,void*v){
  if(!a)return;
  if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_PTR_ARRAY);return;}
  sp_int orig=i; if(i<0)i+=a->len;
  if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));
  while(a->len<=i)sp_PtrArray_push(a,NULL);
  sp_gc_wb((void*)a); a->data[i]=v;
}
static inline sp_int sp_PtrArray_length(sp_PtrArray*a){if(!a)return 0;return a->len;}
static inline sp_bool sp_PtrArray_empty(sp_PtrArray*a){sp_gc_wb((void*)a); if(!a)return TRUE;return a->len==0;}

/* ---- sp_PtrArray cold ops (compiled in lib/sp_array.c) ---- */
void *sp_PtrArray_delete_at(sp_PtrArray *a, sp_int i);
void sp_PtrArray_reverse_bang(sp_PtrArray *a);
void sp_PtrArray_rotate_bang(sp_PtrArray *a, sp_int n);
sp_PtrArray *sp_PtrArray_dup(sp_PtrArray *a);
sp_PtrArray *sp_PtrArray_slice(sp_PtrArray *a, sp_int start, sp_int len);
void sp_PtrArray_shuffle_bang(sp_PtrArray *a);
sp_PtrArray *sp_PtrArray_shuffle(sp_PtrArray *a);
void *sp_PtrArray_sample(sp_PtrArray *a);

/* ============================= sp_StrArray ============================ */
/* Small-array optimization: keep the first SP_STRARR_INLINE elements
   inside the struct so empty/short StrArrays skip the data malloc.
   data == inline_data is the discriminator for "still on inline storage". */
static void sp_StrArray_fin(void*p){sp_StrArray*a=(sp_StrArray*)p;if(a->data!=a->inline_data){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_gc_bytes_sub(sizeof(const char*)*a->cap);h->size-=sizeof(const char*)*a->cap;sp_pl_free(a->data);}}
static void sp_StrArray_scan(void*p){sp_StrArray*a=(sp_StrArray*)p;for(sp_int i=0;i<a->len;i++)sp_mark_string(a->data[i]);}
static sp_StrArray*sp_StrArray_new(void){sp_StrArray*a=(sp_StrArray*)sp_gc_alloc(sizeof(sp_StrArray),sp_StrArray_fin,sp_StrArray_scan);a->cap=SP_STRARR_INLINE;a->data=a->inline_data;a->len=0;return a;}
static inline void sp_StrArray_push(sp_StrArray*a,const char*v){sp_gc_wb((void*)a); if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}if(a->len>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));sp_int nc=((((((a->cap*2))))))+1;if(a->data==a->inline_data){const char**nd=(const char**)sp_pl_alloc(sizeof(const char*)*nc);if(!nd)sp_oom_die();memcpy(nd,a->data,sizeof(const char*)*a->len);a->data=nd;}
else{sp_gc_bytes_sub(sizeof(const char*)*a->cap);h->size-=sizeof(const char*)*a->cap;void*nd=sp_pl_realloc(a->data,sizeof(const char*)*nc);if(!nd)sp_oom_die();a->data=(const char**)nd;}a->cap=nc;h->size+=sizeof(const char*)*a->cap;sp_gc_bytes_add(sizeof(const char*)*a->cap);}a->data[a->len++]=v;}
static inline sp_int sp_StrArray_length(sp_StrArray*a){return a ? a->len : 0;}
static inline sp_bool sp_StrArray_empty(sp_StrArray*a){sp_gc_wb((void*)a); return a->len==0;}
static inline const char*sp_StrArray_get(sp_StrArray*a,sp_int i){if(!a)return NULL;if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;return a->data[i];}
static inline void sp_StrArray_set(sp_StrArray*a,sp_int i,const char*v){sp_gc_wb((void*)a); if(!a)return;if(a->frozen){sp_raise_frozen_array_at(a, SP_BUILTIN_STR_ARRAY);return;}sp_int orig=i;if(i<0)i+=a->len;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));while(i>=a->len)sp_StrArray_push(a,NULL);a->data[i]=v;}  /* the gap is nil, not "" (#3836) */

/* ---- sp_StrArray cold ops (compiled in lib/sp_array.c) ---- */
void sp_StrArray_replace(sp_StrArray *dst, sp_StrArray *src);
const char *sp_StrArray_pop(sp_StrArray *a);
const char *sp_StrArray_shift(sp_StrArray *a);
sp_StrArray *sp_StrArray_slice(sp_StrArray *a, sp_int start, sp_int len);
sp_StrArray *sp_StrArray_slice_range(sp_StrArray *a, sp_int start, sp_int end_, sp_int excl);
void sp_StrArray_reverse_bang(sp_StrArray *a);
void sp_StrArray_rotate_bang(sp_StrArray *a, sp_int n);
void sp_StrArray_sort_bang(sp_StrArray *a);
void sp_StrArray_uniq_bang(sp_StrArray *a);
sp_StrArray *sp_StrArray_uniq(sp_StrArray *a);
const char *sp_StrArray_join(sp_StrArray *a, const char *sep);
sp_bool sp_StrArray_include(sp_StrArray *a, const char *v);
sp_StrArray *sp_StrArray_intersect(sp_StrArray *a, sp_StrArray *b);
sp_bool sp_StrArray_intersect_p(sp_StrArray *a, sp_StrArray *b);
sp_StrArray *sp_StrArray_union(sp_StrArray *a, sp_StrArray *b);
sp_StrArray *sp_StrArray_difference(sp_StrArray *a, sp_StrArray *b);
sp_int sp_StrArray_index(sp_StrArray *a, const char *v);
sp_int sp_StrArray_rindex(sp_StrArray *a, const char *v);
sp_StrArray *sp_StrArray_compact(sp_StrArray *a);
sp_IntArray *sp_IntArray_compact(sp_IntArray *a);
sp_FloatArray *sp_FloatArray_compact(sp_FloatArray *a);
const char *sp_StrArray_delete_at(sp_StrArray *a, sp_int i);
const char *sp_StrArray_delete(sp_StrArray *a, const char *v);
void sp_StrArray_insert(sp_StrArray *a, sp_int i, const char *v);
void sp_StrArray_shuffle_bang(sp_StrArray *a);
sp_StrArray *sp_StrArray_dup(sp_StrArray *a);
sp_StrArray *sp_StrArray_sort(sp_StrArray *a);
sp_StrArray *sp_StrArray_shuffle(sp_StrArray *a);
const char *sp_StrArray_sample(sp_StrArray *a);

/* ---- poly/inspect-dependent ops (lib/sp_array.c; need sp_inspect.h/sp_str.h) ---- */
sp_StrArray *sp_StrArray_from_string_range(const char *s, const char *e, sp_int excl);
const char*sp_IntArray_inspect(sp_IntArray*a);
const char*sp_FloatArray_inspect(sp_FloatArray*a);
const char*sp_FloatArray_join(sp_FloatArray*a,const char*sep);
sp_bool sp_FloatArray_eq(sp_FloatArray*a,sp_FloatArray*b);
const char*sp_StrArray_inspect(sp_StrArray*a);
const char*sp_PtrArray_inspect(sp_PtrArray*a);
sp_PtrArray*sp_IntArray_slice_before(sp_IntArray*a,sp_int d);
sp_PtrArray*sp_IntArray_slice_after(sp_IntArray*a,sp_int d);
sp_PtrArray *sp_IntArray_product(sp_IntArray *a, sp_IntArray *b);
const char*sp_PtrArray_str_join(sp_PtrArray*a,const char*sep);
sp_RbVal sp_IntArray_index_poly(sp_IntArray *a, sp_int v);
sp_RbVal sp_IntArray_rindex_poly(sp_IntArray *a, sp_int v);
sp_RbVal sp_StrArray_index_poly(sp_StrArray *a, const char *v);
sp_RbVal sp_StrArray_rindex_poly(sp_StrArray *a, const char *v);
sp_int sp_IntArray_index_opt(sp_IntArray *a, sp_int v);
sp_int sp_IntArray_rindex_opt(sp_IntArray *a, sp_int v);
sp_RbVal sp_FloatArray_index_poly(sp_FloatArray *a, sp_float v);
sp_RbVal sp_FloatArray_rindex_poly(sp_FloatArray *a, sp_float v);
sp_RbVal sp_FloatArray_index_key(sp_FloatArray *a, sp_RbVal v);
sp_RbVal sp_FloatArray_rindex_key(sp_FloatArray *a, sp_RbVal v);
sp_float sp_FloatArray_delete_key(sp_FloatArray *a, sp_RbVal v);
const int64_t *sp_IntArray_ffi_data(sp_IntArray *a);
const double *sp_FloatArray_ffi_data(sp_FloatArray *a);
sp_IntArray *sp_IntArray_concat(sp_IntArray *a, sp_IntArray *b);
sp_StrArray *sp_StrArray_concat(sp_StrArray *a, sp_StrArray *b);
sp_FloatArray *sp_FloatArray_concat(sp_FloatArray *a, sp_FloatArray *b);
sp_PolyArray *sp_IntArray_to_poly(sp_IntArray *a);
sp_PolyArray *sp_StrArray_to_poly_fmt(sp_StrArray *a);
sp_PolyArray *sp_FloatArray_to_poly(sp_FloatArray *a);
sp_IntArray *sp_IntArray_slice_bang(sp_IntArray *a, sp_int from, sp_int n);
sp_FloatArray *sp_FloatArray_slice_bang(sp_FloatArray *a, sp_int from, sp_int n);
sp_StrArray *sp_StrArray_slice_bang(sp_StrArray *a, sp_int from, sp_int n);
sp_PtrArray *sp_PtrArray_slice_bang(sp_PtrArray *a, sp_int from, sp_int n);

/* ---- more cold StrArray ops relocated from spinel_rt.h (0 optcarrot
   uses). #include here (not near the top): by this point array.h's own
   hot inline core (sp_StrArray_push et al) is already defined, so
   sp_str.h's nested processing (it needs sp_StrArray_push for
   sp_str_split_push) sees it; the reverse order undeclares it. ---- */
#include "sp_str.h"     /* sp_str_eq, for the cold StrArray ops below */
const char *sp_StrArray_sum_str(sp_StrArray *a, const char *init);
sp_RbVal sp_StrArray_uniq_bangq(sp_StrArray *a);
sp_bool sp_StrArray_eq(sp_StrArray*a,sp_StrArray*b);

/* ---- sp_typed_arr_frozen relocated from spinel_rt.h (0 optcarrot uses). ---- */
int sp_typed_arr_frozen(sp_RbVal v);

#endif /* SP_ARRAY_H */

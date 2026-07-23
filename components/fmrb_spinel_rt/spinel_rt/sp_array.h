#ifndef SP_ARRAY_H
#define SP_ARRAY_H
/* sp_array.h -- typed array hot core + cold-op surface.
 *
 * The struct layouts (sp_IntArray, ...) live in sp_types.h. The hot
 * accessors (new / push / pop / shift / get / set / length / empty) stay
 * inline here so every generated TU compiles them identically --
 * relocating them out of sp_runtime.h into this shared header is a pure
 * textual move with no codegen change. The cold ops (sort / slice / dup /
 * set algebra / join / ...) are compiled once into libspinel_rt.a
 * (lib/sp_array.c); this header only declares them.
 *
 * sp_sprintf / sp_raise_cls / sp_raise_frozen_array are provided by the
 * generated TU and resolved at the final link, the same way lib/sp_core.c
 * calls them -- so lib/sp_array.c can use them without a runtime include.
 */
#include "sp_gc.h"      /* sp_gc_hdr, sp_gc_bytes, SP_GC_ROOT, sp_oom_die */
#include "sp_alloc.h"   /* sp_gc_alloc, sp_str_alloc, sp_raise_cls, sp_raise_frozen_array */

const char *sp_sprintf(const char *fmt, ...);  /* defined in the generated TU */

/* ============================ sp_IntArray ============================ */
/* `frozen` rides in the struct (not the GC header) so the hot push /
   []= paths read it from the same cache line as len/cap -- no extra
   cache miss vs. the GC-header bit. calloc in sp_gc_alloc zero-inits
   it, so constructors need no change. Issue #918. */
static void sp_IntArray_fin(void*p){free(((sp_IntArray*)p)->data);}
static sp_IntArray*sp_IntArray_new(void){sp_IntArray*a=(sp_IntArray*)sp_gc_alloc(sizeof(sp_IntArray),sp_IntArray_fin,NULL);a->cap=16;a->data=(mrb_int*)malloc(sizeof(mrb_int)*a->cap);if(!a->data)sp_oom_die();a->start=0;a->len=0;{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));h->size+=sizeof(mrb_int)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*a->cap);}return a;}
static void __attribute__((noinline)) sp_IntArray_push_grow(sp_IntArray*a){if(a->start>0){memmove(a->data,a->data+a->start,sizeof(mrb_int)*a->len);a->start=0;if(a->len<a->cap)return;}{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*a->cap);h->size-=sizeof(mrb_int)*a->cap;a->cap=((((((a->cap*2))))))+1;void*nd=realloc(a->data,sizeof(mrb_int)*a->cap);if(!nd)sp_oom_die();a->data=(mrb_int*)nd;h->size+=sizeof(mrb_int)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*a->cap);}}
static inline void sp_IntArray_push(sp_IntArray*a,mrb_int v){if(a->frozen){sp_raise_frozen_array();return;}if(a->start+a->len>=a->cap)sp_IntArray_push_grow(a);a->data[a->start+a->len]=v;a->len++;}
/* Issue #826/#832: empty pop/shift return SP_INT_NIL (nullable int
   sentinel) to match MRI's nil; callers treat as int?. Without the
   guard, `--a->len` wraps to -1 and reads past the buffer start. */
static inline mrb_int sp_IntArray_pop(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;if(a->frozen){sp_raise_frozen_array();return SP_INT_NIL;}return a->data[a->start+--a->len];}
static inline mrb_int sp_IntArray_shift(sp_IntArray*a){if(!a||a->len<=0)return SP_INT_NIL;if(a->frozen){sp_raise_frozen_array();return SP_INT_NIL;}mrb_int v=a->data[a->start];a->start++;a->len--;return v;}
static inline mrb_int sp_IntArray_length(sp_IntArray*a){return a->len;}
static inline mrb_bool sp_IntArray_empty(sp_IntArray*a){return a->len==0;}
static inline mrb_int sp_IntArray_get(sp_IntArray*a,mrb_int i){if(!a)return SP_INT_NIL;if(i<0)i+=a->len;if(i<0||i>=a->len)return SP_INT_NIL;return a->data[a->start+i];}
/* Issue #769: a very-negative i leaves i negative after the `i += a->len`
   adjustment. CRuby raises IndexError; spinel no-ops as the safest
   fallback (raising from a typed-array set would need setjmp plumbing
   throughout the call chain). */
static void sp_IntArray_set_slow(sp_IntArray*a,mrb_int i,mrb_int v){if(i<0)return;while(a->start+i>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_int)*a->cap);h->size-=sizeof(mrb_int)*a->cap;a->cap=((((((a->cap*2))))))+1;a->data=(mrb_int*)realloc(a->data,sizeof(mrb_int)*a->cap);h->size+=sizeof(mrb_int)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_int)*a->cap);}while(i>=a->len){a->data[a->start+a->len]=SP_INT_NIL;a->len++;}  /* gap slots read as nil */a->data[a->start+i]=v;}
/* Issue #839: an extreme negative index (still negative after `i += len`)
   raises IndexError per MRI. */
static inline void sp_IntArray_set(sp_IntArray*a,mrb_int i,mrb_int v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}mrb_int orig=i;if(i<0)i+=a->len;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));if(i<a->len){a->data[a->start+i]=v;return;}sp_IntArray_set_slow(a,i,v);}

/* ---- sp_IntArray cold ops (compiled in lib/sp_array.c) ---- */
sp_IntArray *sp_IntArray_from_range(mrb_int s, mrb_int e);
sp_IntArray *sp_IntArray_from_range_step(mrb_int beg, mrb_int end, mrb_int step, mrb_int excl);
sp_IntArray *sp_IntArray_dup(sp_IntArray *a);
sp_IntArray *sp_IntArray_slice(sp_IntArray *a, mrb_int start, mrb_int len);
sp_IntArray *sp_IntArray_slice_range(sp_IntArray *a, mrb_int start, mrb_int end_, mrb_int excl);
void sp_IntArray_replace(sp_IntArray *dst, sp_IntArray *src);
void sp_IntArray_splice(sp_IntArray *a, mrb_int start, mrb_int len, const mrb_int *src, mrb_int srcn);
void sp_FloatArray_splice(sp_FloatArray *a, mrb_int start, mrb_int len, const mrb_float *src, mrb_int srcn);
void sp_StrArray_splice(sp_StrArray *a, mrb_int start, mrb_int len, const char *const *src, mrb_int srcn);
void sp_PolyArray_splice(sp_PolyArray *a, mrb_int start, mrb_int len, sp_RbVal src);
void sp_IntArray_reverse_bang(sp_IntArray *a);
void sp_IntArray_rotate_bang(sp_IntArray *a, mrb_int n);
sp_IntArray *sp_IntArray_sort(sp_IntArray *a);
void sp_IntArray_sort_bang(sp_IntArray *a);
void sp_IntArray_uniq_bang(sp_IntArray *a);
void sp_IntArray_shuffle_bang(sp_IntArray *a);
sp_IntArray *sp_IntArray_shuffle(sp_IntArray *a);
mrb_int sp_IntArray_sample(sp_IntArray *a);
mrb_int sp_IntArray_min(sp_IntArray *a);
mrb_int sp_IntArray_max(sp_IntArray *a);
const char *sp_StrArray_min(sp_StrArray *a);
const char *sp_StrArray_max(sp_StrArray *a);
mrb_int sp_IntArray_sum(sp_IntArray *a, mrb_int init);
mrb_bool sp_IntArray_include(sp_IntArray *a, mrb_int v);
mrb_int sp_IntArray_index(sp_IntArray *a, mrb_int v);
mrb_int sp_IntArray_rindex(sp_IntArray *a, mrb_int v);
mrb_int sp_IntArray_delete_at(sp_IntArray *a, mrb_int i);
mrb_int sp_IntArray_delete(sp_IntArray *a, mrb_int v);
void sp_IntArray_insert(sp_IntArray *a, mrb_int i, mrb_int v);
sp_IntArray *sp_IntArray_uniq(sp_IntArray *a);
sp_IntArray *sp_IntArray_intersect(sp_IntArray *a, sp_IntArray *b);
mrb_bool sp_IntArray_intersect_p(sp_IntArray *a, sp_IntArray *b);
sp_IntArray *sp_IntArray_union(sp_IntArray *a, sp_IntArray *b);
sp_IntArray *sp_IntArray_difference(sp_IntArray *a, sp_IntArray *b);
void sp_IntArray_unshift(sp_IntArray *a, mrb_int v);
const char *sp_IntArray_join(sp_IntArray *a, const char *sep);
mrb_bool sp_IntArray_eq(sp_IntArray *a, sp_IntArray *b);
mrb_int sp_IntArray_cmp(sp_IntArray *a, sp_IntArray *b);

/* =========================== sp_FloatArray =========================== */
static void sp_FloatArray_fin(void*p){sp_FloatArray*a=(sp_FloatArray*)p;sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_float)*a->cap);h->size-=sizeof(mrb_float)*a->cap;free(a->data);}
static sp_FloatArray*sp_FloatArray_new(void){sp_FloatArray*a=(sp_FloatArray*)sp_gc_alloc(sizeof(sp_FloatArray),sp_FloatArray_fin,NULL);a->cap=16;a->data=(mrb_float*)malloc(sizeof(mrb_float)*a->cap);if(!a->data)sp_oom_die();a->len=0;{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));h->size+=sizeof(mrb_float)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_float)*a->cap);}return a;}
static inline void sp_FloatArray_push(sp_FloatArray*a,mrb_float v){if(a->frozen){sp_raise_frozen_array();return;}if(a->len>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(mrb_float)*a->cap);h->size-=sizeof(mrb_float)*a->cap;a->cap=((((((a->cap*2))))))+1;a->data=(mrb_float*)realloc(a->data,sizeof(mrb_float)*a->cap);h->size+=sizeof(mrb_float)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(mrb_float)*a->cap);}a->data[a->len++]=v;}
static inline mrb_float sp_FloatArray_pop(sp_FloatArray*a){if(!a||a->len<=0)return 0.0;if(a->frozen){sp_raise_frozen_array();return 0.0;}return a->data[--a->len];}
static inline mrb_float sp_FloatArray_shift(sp_FloatArray*a){if(!a||a->len==0)return 0.0;if(a->frozen){sp_raise_frozen_array();return 0.0;}mrb_float v=a->data[0];for(mrb_int i=0;i+1<a->len;i++)a->data[i]=a->data[i+1];a->len--;return v;}
/* FloatArray is 0-based (no `start` offset, unlike IntArray). delete_at
   returns 0.0 on out-of-range (delete_at's nil there). */
static inline mrb_float sp_FloatArray_delete_at(sp_FloatArray*a,mrb_int i){if(!a)return 0.0;if(a->frozen){sp_raise_frozen_array();return 0.0;}if(i<0)i+=a->len;if(i<0||i>=a->len)return 0.0;mrb_float v=a->data[i];for(mrb_int j=i;j+1<a->len;j++)a->data[j]=a->data[j+1];a->len--;return v;}
static inline mrb_int sp_FloatArray_length(sp_FloatArray*a){return a->len;}
static inline mrb_bool sp_FloatArray_empty(sp_FloatArray*a){return a->len==0;}
static inline mrb_float sp_FloatArray_get(sp_FloatArray*a,mrb_int i){if(!a)return sp_float_nil();if(i<0)i+=a->len;if(i<0||i>=a->len)return sp_float_nil();return a->data[i];}
/* first/last as float? : nil (sentinel) when empty, else the element.
   `[i]` stays non-nullable (0.0 for OOB) -- only first/last produce nil. */
static inline mrb_float sp_FloatArray_first_opt(sp_FloatArray*a){return (!a||a->len<=0)?sp_float_nil():sp_FloatArray_get(a,0);}
static inline mrb_float sp_FloatArray_last_opt(sp_FloatArray*a){return (!a||a->len<=0)?sp_float_nil():sp_FloatArray_get(a,a->len-1);}
/* Issue #769: no-op for negative index after adjustment. */
static inline void sp_FloatArray_set(sp_FloatArray*a,mrb_int i,mrb_float v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}mrb_int orig=i;if(i<0)i+=a->len;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));while(i>=a->cap){a->cap=((((((a->cap*2))))))+1;a->data=(mrb_float*)realloc(a->data,sizeof(mrb_float)*a->cap);}while(i>=a->len){a->data[a->len]=0.0;a->len++;}a->data[i]=v;}

/* ---- sp_FloatArray cold ops (compiled in lib/sp_array.c) ---- */
void sp_FloatArray_unshift(sp_FloatArray *a, mrb_float v);
sp_FloatArray *sp_FloatArray_from_step(mrb_float beg, mrb_float end, mrb_float step, mrb_int excl);
mrb_float sp_FloatArray_min(sp_FloatArray *a);
mrb_float sp_FloatArray_max(sp_FloatArray *a);
mrb_float sp_FloatArray_sum(sp_FloatArray *a, mrb_float init);
void sp_FloatArray_replace(sp_FloatArray *dst, sp_FloatArray *src);
sp_FloatArray *sp_FloatArray_slice(sp_FloatArray *a, mrb_int start, mrb_int len);
sp_FloatArray *sp_FloatArray_slice_range(sp_FloatArray *a, mrb_int start, mrb_int end_, mrb_int excl);
void sp_FloatArray_reverse_bang(sp_FloatArray *a);
void sp_FloatArray_rotate_bang(sp_FloatArray *a, mrb_int n);
void sp_FloatArray_sort_bang(sp_FloatArray *a);
void sp_FloatArray_shuffle_bang(sp_FloatArray *a);
sp_FloatArray *sp_FloatArray_dup(sp_FloatArray *a);
sp_FloatArray *sp_FloatArray_sort(sp_FloatArray *a);
sp_FloatArray *sp_FloatArray_shuffle(sp_FloatArray *a);
mrb_float sp_FloatArray_sample(sp_FloatArray *a);
mrb_bool sp_FloatArray_include(sp_FloatArray *a, mrb_float v);
sp_FloatArray *sp_FloatArray_intersect(sp_FloatArray *a, sp_FloatArray *b);
mrb_bool sp_FloatArray_intersect_p(sp_FloatArray *a, sp_FloatArray *b);
sp_FloatArray *sp_FloatArray_union(sp_FloatArray *a, sp_FloatArray *b);
sp_FloatArray *sp_FloatArray_difference(sp_FloatArray *a, sp_FloatArray *b);
sp_FloatArray *sp_FloatArray_uniq(sp_FloatArray *a);

/* ============================= sp_PtrArray ============================ */
/* Array of void* pointers (user-class arrays, FFI pointer arrays). */
static void sp_PtrArray_fin(void*p){sp_PtrArray*a=(sp_PtrArray*)p;sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(void*)*a->cap);h->size-=sizeof(void*)*a->cap;free(a->data);}
static void sp_PtrArray_gc_scan(void*p){sp_PtrArray*a=(sp_PtrArray*)p;if(!a->scan_elem)return;for(mrb_int i=0;i<a->len;i++){if(a->data[i])a->scan_elem(a->data[i]);}}
static sp_PtrArray*sp_PtrArray_new_scan(void(*scan_elem)(void*)){sp_PtrArray*a=(sp_PtrArray*)sp_gc_alloc(sizeof(sp_PtrArray),sp_PtrArray_fin,scan_elem?sp_PtrArray_gc_scan:NULL);a->cap=16;a->data=(void**)malloc(sizeof(void*)*a->cap);if(!a->data)sp_oom_die();a->len=0;a->scan_elem=scan_elem;{sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));h->size+=sizeof(void*)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(void*)*a->cap);}return a;}
static sp_PtrArray*sp_PtrArray_new(void){return sp_PtrArray_new_scan(sp_gc_mark);}
/* PtrArray for raw external pointers (FFI `:ptr` returns, dlopen handles).
   These don't carry sp_gc_hdr -- the default sp_gc_mark element scan would
   read undefined bytes and crash at collection. Skip per-element scanning;
   the array header itself is still GC-tracked. */
static sp_PtrArray*sp_PtrArray_new_noscan(void){return sp_PtrArray_new_scan(NULL);}
static inline void sp_PtrArray_push(sp_PtrArray*a,void*v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}if(a->len>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(void*)*a->cap);h->size-=sizeof(void*)*a->cap;a->cap=((((((a->cap*2))))))+1;void*nd=realloc(a->data,sizeof(void*)*a->cap);if(!nd)sp_oom_die();a->data=(void**)nd;h->size+=sizeof(void*)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(void*)*a->cap);}a->data[a->len++]=v;}
/* Array#pop on a `<X>_ptr_array`. Returns NULL when empty (matches CRuby's
   nil for typed-element arrays since the slot can't carry nil). #520. */
static inline void *sp_PtrArray_pop(sp_PtrArray*a){if(!a||a->len==0)return NULL;return a->data[--a->len];}
static inline void*sp_PtrArray_get(sp_PtrArray*a,mrb_int i){if(!a)return NULL;if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;return a->data[i];}
/* Issue #770: bounds-check the final index; no-op out-of-range rather
   than writing into adjacent memory (typed slots have a fixed shape). */
static inline void sp_PtrArray_set(sp_PtrArray*a,mrb_int i,void*v){if(!a)return;if(i<0)i+=a->len;if(i<0||i>=a->len)return;a->data[i]=v;}
static inline mrb_int sp_PtrArray_length(sp_PtrArray*a){if(!a)return 0;return a->len;}
static inline mrb_bool sp_PtrArray_empty(sp_PtrArray*a){if(!a)return TRUE;return a->len==0;}

/* ---- sp_PtrArray cold ops (compiled in lib/sp_array.c) ---- */
void *sp_PtrArray_delete_at(sp_PtrArray *a, mrb_int i);
void sp_PtrArray_reverse_bang(sp_PtrArray *a);
void sp_PtrArray_rotate_bang(sp_PtrArray *a, mrb_int n);
sp_PtrArray *sp_PtrArray_dup(sp_PtrArray *a);
sp_PtrArray *sp_PtrArray_slice(sp_PtrArray *a, mrb_int start, mrb_int len);
void sp_PtrArray_shuffle_bang(sp_PtrArray *a);
sp_PtrArray *sp_PtrArray_shuffle(sp_PtrArray *a);
void *sp_PtrArray_sample(sp_PtrArray *a);

/* ============================= sp_StrArray ============================ */
/* Small-array optimization: keep the first SP_STRARR_INLINE elements
   inside the struct so empty/short StrArrays skip the data malloc.
   data == inline_data is the discriminator for "still on inline storage". */
static void sp_StrArray_fin(void*p){sp_StrArray*a=(sp_StrArray*)p;if(a->data!=a->inline_data){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));SP_GC_CTR_SUB(sp_gc_bytes,sizeof(const char*)*a->cap);h->size-=sizeof(const char*)*a->cap;free(a->data);}}
static void sp_StrArray_scan(void*p){sp_StrArray*a=(sp_StrArray*)p;for(mrb_int i=0;i<a->len;i++)sp_mark_string(a->data[i]);}
static sp_StrArray*sp_StrArray_new(void){sp_StrArray*a=(sp_StrArray*)sp_gc_alloc(sizeof(sp_StrArray),sp_StrArray_fin,sp_StrArray_scan);a->cap=SP_STRARR_INLINE;a->data=a->inline_data;a->len=0;return a;}
static inline void sp_StrArray_push(sp_StrArray*a,const char*v){if(a->frozen){sp_raise_frozen_array();return;}if(a->len>=a->cap){sp_gc_hdr*h=(sp_gc_hdr*)((char*)a-sizeof(sp_gc_hdr));mrb_int nc=((((((a->cap*2))))))+1;if(a->data==a->inline_data){const char**nd=(const char**)malloc(sizeof(const char*)*nc);if(!nd)sp_oom_die();memcpy(nd,a->data,sizeof(const char*)*a->len);a->data=nd;}else{SP_GC_CTR_SUB(sp_gc_bytes,sizeof(const char*)*a->cap);h->size-=sizeof(const char*)*a->cap;void*nd=realloc(a->data,sizeof(const char*)*nc);if(!nd)sp_oom_die();a->data=(const char**)nd;}a->cap=nc;h->size+=sizeof(const char*)*a->cap;SP_GC_CTR_ADD(sp_gc_bytes,sizeof(const char*)*a->cap);}a->data[a->len++]=v;}
static inline mrb_int sp_StrArray_length(sp_StrArray*a){return a->len;}
static inline mrb_bool sp_StrArray_empty(sp_StrArray*a){return a->len==0;}
static inline const char*sp_StrArray_get(sp_StrArray*a,mrb_int i){if(!a)return NULL;if(i<0)i+=a->len;if(i<0||i>=a->len)return NULL;return a->data[i];}
static inline void sp_StrArray_set(sp_StrArray*a,mrb_int i,const char*v){if(!a)return;if(a->frozen){sp_raise_frozen_array();return;}mrb_int orig=i;if(i<0)i+=a->len;if(i<0)sp_raise_cls("IndexError",sp_sprintf("index %lld too small for array; minimum: %lld",(long long)orig,(long long)-a->len));while(i>=a->len)sp_StrArray_push(a,sp_str_empty);a->data[i]=v;}

/* ---- sp_StrArray cold ops (compiled in lib/sp_array.c) ---- */
void sp_StrArray_replace(sp_StrArray *dst, sp_StrArray *src);
const char *sp_StrArray_pop(sp_StrArray *a);
const char *sp_StrArray_shift(sp_StrArray *a);
sp_StrArray *sp_StrArray_slice(sp_StrArray *a, mrb_int start, mrb_int len);
sp_StrArray *sp_StrArray_slice_range(sp_StrArray *a, mrb_int start, mrb_int end_, mrb_int excl);
void sp_StrArray_reverse_bang(sp_StrArray *a);
void sp_StrArray_rotate_bang(sp_StrArray *a, mrb_int n);
void sp_StrArray_sort_bang(sp_StrArray *a);
void sp_StrArray_uniq_bang(sp_StrArray *a);
sp_StrArray *sp_StrArray_uniq(sp_StrArray *a);
const char *sp_StrArray_join(sp_StrArray *a, const char *sep);
mrb_bool sp_StrArray_include(sp_StrArray *a, const char *v);
sp_StrArray *sp_StrArray_intersect(sp_StrArray *a, sp_StrArray *b);
mrb_bool sp_StrArray_intersect_p(sp_StrArray *a, sp_StrArray *b);
sp_StrArray *sp_StrArray_union(sp_StrArray *a, sp_StrArray *b);
sp_StrArray *sp_StrArray_difference(sp_StrArray *a, sp_StrArray *b);
mrb_int sp_StrArray_index(sp_StrArray *a, const char *v);
mrb_int sp_StrArray_rindex(sp_StrArray *a, const char *v);
sp_StrArray *sp_StrArray_compact(sp_StrArray *a);
const char *sp_StrArray_delete_at(sp_StrArray *a, mrb_int i);
const char *sp_StrArray_delete(sp_StrArray *a, const char *v);
void sp_StrArray_insert(sp_StrArray *a, mrb_int i, const char *v);
void sp_StrArray_shuffle_bang(sp_StrArray *a);
sp_StrArray *sp_StrArray_dup(sp_StrArray *a);
sp_StrArray *sp_StrArray_sort(sp_StrArray *a);
sp_StrArray *sp_StrArray_shuffle(sp_StrArray *a);
const char *sp_StrArray_sample(sp_StrArray *a);

/* ---- poly/inspect-dependent ops (lib/sp_array.c; need sp_inspect.h/sp_str.h) ---- */
sp_StrArray *sp_StrArray_from_string_range(const char *s, const char *e, mrb_int excl);
const char*sp_IntArray_inspect(sp_IntArray*a);
const char*sp_FloatArray_inspect(sp_FloatArray*a);
const char*sp_FloatArray_join(sp_FloatArray*a,const char*sep);
mrb_bool sp_FloatArray_eq(sp_FloatArray*a,sp_FloatArray*b);
const char*sp_StrArray_inspect(sp_StrArray*a);
const char*sp_PtrArray_inspect(sp_PtrArray*a);
sp_PtrArray*sp_IntArray_slice_before(sp_IntArray*a,mrb_int d);
sp_PtrArray*sp_IntArray_slice_after(sp_IntArray*a,mrb_int d);
sp_PtrArray *sp_IntArray_product(sp_IntArray *a, sp_IntArray *b);
const char*sp_PtrArray_str_join(sp_PtrArray*a,const char*sep);
sp_RbVal sp_IntArray_index_poly(sp_IntArray *a, mrb_int v);
sp_RbVal sp_IntArray_rindex_poly(sp_IntArray *a, mrb_int v);
sp_RbVal sp_StrArray_index_poly(sp_StrArray *a, const char *v);
sp_RbVal sp_StrArray_rindex_poly(sp_StrArray *a, const char *v);
mrb_int sp_IntArray_index_opt(sp_IntArray *a, mrb_int v);
mrb_int sp_IntArray_rindex_opt(sp_IntArray *a, mrb_int v);
const int64_t *sp_IntArray_ffi_data(sp_IntArray *a);
const double *sp_FloatArray_ffi_data(sp_FloatArray *a);
sp_IntArray *sp_IntArray_concat(sp_IntArray *a, sp_IntArray *b);
sp_StrArray *sp_StrArray_concat(sp_StrArray *a, sp_StrArray *b);
sp_FloatArray *sp_FloatArray_concat(sp_FloatArray *a, sp_FloatArray *b);
sp_PolyArray *sp_IntArray_to_poly(sp_IntArray *a);
sp_PolyArray *sp_StrArray_to_poly_fmt(sp_StrArray *a);
sp_PolyArray *sp_FloatArray_to_poly(sp_FloatArray *a);
sp_IntArray *sp_IntArray_slice_bang(sp_IntArray *a, mrb_int from, mrb_int n);
sp_FloatArray *sp_FloatArray_slice_bang(sp_FloatArray *a, mrb_int from, mrb_int n);
sp_StrArray *sp_StrArray_slice_bang(sp_StrArray *a, mrb_int from, mrb_int n);
sp_PtrArray *sp_PtrArray_slice_bang(sp_PtrArray *a, mrb_int from, mrb_int n);

#endif /* SP_ARRAY_H */

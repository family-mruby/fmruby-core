#ifndef SP_STRING_H
#define SP_STRING_H
/* sp_string.h -- the mutable-String builder (sp_String), shared so both the
   generated TU and cold lib C files (inspect helpers, ...) can build strings.

   The hot core -- construction, append, and the sp_fd_* buffer mechanics that
   sit on the string concat / interpolation path -- stays `static inline` here so
   every TU inlines it (no perf cost vs. living in sp_runtime.h). The rarely-used
   in-place mutators (prepend / insert / replace / dup) are cold, so they are
   compiled once into libspinel_rt.a (lib/sp_string.c). */
#include "sp_alloc.h"   /* sp_gc_alloc, sp_gc_bytes/hdr, sp_str_hdr, sp_str_byte_len, sp_raise_cls */
#include <string.h>

typedef struct { char *data; int64_t len; int64_t cap; } sp_String;

/* Per-mutable-string freeze flag rides in the GC header alongside `marked`. */
static inline mrb_bool sp_String_is_frozen(sp_String*s){if(!s)return TRUE;sp_gc_hdr*h=(sp_gc_hdr*)((char*)s-sizeof(sp_gc_hdr));return h->frozen;}
static inline sp_String*sp_String_freeze(sp_String*s){if(s){sp_gc_hdr*h=(sp_gc_hdr*)((char*)s-sizeof(sp_gc_hdr));h->frozen=1;}return s;}

/* A mutable String's payload carries the same length-bearing sp_str_hdr that
   0xfe/0xfc heap strings use, so an escaped const char* is binary-safe (the
   reader uses sp_str_byte_len, not strlen). Block layout:
   [sp_str_hdr][0xfd marker][data ...][NUL]. s->data points at `data`. The ctor
   and in-place mutators are also reached with bare C string literals (no marker
   byte), so they size their operand with strlen, not a [-1]-marker reader. */
#define SP_FD_HDR (sizeof(sp_str_hdr)+1)   /* header + marker byte */
#define SP_FD_OVH (sizeof(sp_str_hdr)+2)   /* header + marker + NUL terminator */
static inline char *sp_fd_base(const char *data){return (char*)data-SP_FD_HDR;}
static inline char *sp_fd_setup(char *raw){
  sp_str_hdr *h = (sp_str_hdr *)raw;
  h->next = NULL; h->size = 0; h->len = 0; h->hash = 0;
  char *body = (char *)(h + 1);
  body[0] = (char)0xfd;
  return body + 1;
}
static inline void sp_fd_publish(sp_String *s){
  sp_str_hdr *h = (sp_str_hdr *)sp_fd_base(s->data);
  h->len = (uint32_t)s->len; h->hash = 0;
}
static inline int sp_fd_grow(sp_String *s, int64_t need){
  if (need < s->cap) return 1;
  sp_gc_hdr *h = (sp_gc_hdr *)((char *)s - sizeof(sp_gc_hdr));
  int64_t new_cap = (need * 2) + 16;
  char *raw = (char *)realloc(sp_fd_base(s->data), SP_FD_OVH + new_cap);
  if (!raw) return 0;
  SP_GC_CTR_SUB(sp_gc_bytes, s->cap + SP_FD_OVH); h->size -= s->cap + SP_FD_OVH;
  s->cap = new_cap; s->data = sp_fd_setup(raw);
  h->size += s->cap + SP_FD_OVH; SP_GC_CTR_ADD(sp_gc_bytes, s->cap + SP_FD_OVH);
  return 1;
}
static inline void sp_String_fin(void*p){free(sp_fd_base(((sp_String*)p)->data));}
static inline sp_String*sp_String_new(const char*s){
  /* Copy s's payload into a raw-malloc'd buffer BEFORE sp_gc_alloc: if s is a
     heap string anchored only by this stack frame, sp_gc_alloc can trigger a
     collection that frees it mid-call. The malloc'd buffer is off both heaps. */
  int64_t len=(int64_t)strlen(s);
  int64_t cap=(len*2)+16;
  char*raw=(char*)malloc(SP_FD_OVH+cap);
  char*data=sp_fd_setup(raw);
  memcpy(data,s,len);data[len]=0;
  sp_String*r=(sp_String*)sp_gc_alloc(sizeof(sp_String),sp_String_fin,NULL);
  r->len=len;r->cap=cap;r->data=data;
  {sp_gc_hdr*h=(sp_gc_hdr*)((char*)r-sizeof(sp_gc_hdr));h->size+=r->cap+SP_FD_OVH;SP_GC_CTR_ADD(sp_gc_bytes,r->cap+SP_FD_OVH);}
  sp_fd_publish(r);
  return r;
}
/* Shared append core: `tl` is the operand byte length (strlen for the
   bare-literal-safe entry, sp_str_byte_len for the binary one). */
static inline void sp_fd_append_len(sp_String*s,const char*t,int64_t tl){if(!sp_fd_grow(s,s->len+tl))return;memcpy(s->data+s->len,t,tl);s->len+=tl;s->data[s->len]=0;sp_fd_publish(s);}
static inline void sp_String_append(sp_String*s,const char*t){if(!s||!t)return;if(sp_String_is_frozen(s)){sp_raise_frozen_str(s->data);return;}sp_fd_append_len(s,t,(int64_t)strlen(t));}
/* Binary-safe append: sizes the operand with the header length so an embedded
   NUL is preserved (Ruby String#<< / concat on a marked spinel string). */
static inline void sp_String_append_bin(sp_String*s,const char*t){if(!s||!t)return;if(sp_String_is_frozen(s)){sp_raise_frozen_str(s->data);return;}sp_fd_append_len(s,t,(int64_t)sp_str_byte_len(t));}
static inline const char*sp_String_cstr(sp_String*s){return s->data;}
static inline int64_t sp_String_length(sp_String*s){return s->len;}

/* Cold in-place mutators (compiled once in lib/sp_string.c). */
void sp_String_prepend(sp_String*s,const char*t);
void sp_String_insert(sp_String*s,int64_t idx,const char*t);
void sp_String_replace(sp_String*s,const char*t);
sp_String*sp_String_dup(sp_String*s);
#endif /* SP_STRING_H */

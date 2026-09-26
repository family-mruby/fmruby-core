#ifndef SP_STRING_H
#define SP_STRING_H
/* sp_string.h -- the mutable-String builder (sp_String), shared so both the
   generated TU and cold lib C files (inspect helpers, ...) can build strings.

   The hot core -- construction, append, and the sp_fd_* buffer mechanics that
   sit on the string concat / interpolation path -- stays `static inline` here so
   every TU inlines it (no perf cost vs. living in spinel_rt.h). The rarely-used
   in-place mutators (prepend / insert / replace / dup) are cold, so they are
   compiled once into libspinel_rt.a (lib/sp_string.c). */
#include "sp_alloc.h"   /* sp_gc_alloc, sp_gc_bytes/hdr, sp_str_hdr, sp_str_byte_len, sp_raise_cls */
#include <string.h>

/* `binary` is the ASCII-8BIT tag, kept on the HANDLE: sp_fd_setup zeroes the
   payload header on every grow, so the tag has to be re-stamped after each
   mutation rather than living only in the bytes. */
typedef struct { char *data; int64_t len; int64_t cap; unsigned binary; } sp_String;

/* Per-mutable-string freeze flag rides in the GC header alongside `marked`. */
static inline sp_bool sp_String_is_frozen(sp_String*s){if(!s)return TRUE;sp_gc_hdr*h=(sp_gc_hdr*)((char*)s-sizeof(sp_gc_hdr));return h->frozen;}
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
/* The payload's `next` field carries the owning sp_String rather than a heap
   link: these blocks are malloc'd and never join the string heap, so nothing
   walks that list, and a container holding the escaped `const char *` has no
   other way back to the handle. Without it the mark reaches the bytes and
   stops, the handle goes unreferenced, and sp_String_fin frees the bytes the
   container still points at. sp_fd_own publishes it; every path that lays out
   a payload has to call it. */
static inline char *sp_fd_setup(char *raw){
  sp_str_hdr *h = (sp_str_hdr *)raw;
  h->next = NULL; h->size = 0; h->len = 0; h->hash = 0;
  char *body = (char *)(h + 1);
  body[0] = (char)0xfd;
  return body + 1;
}
static inline void sp_fd_own(sp_String *s){
  ((sp_str_hdr *)sp_fd_base(s->data))->next = (sp_str_hdr *)(void *)s;
}
static inline void sp_fd_publish(sp_String *s){
  sp_str_hdr *h = (sp_str_hdr *)sp_fd_base(s->data);
  h->len = (uint32_t)s->len; h->hash = 0;
  h->size &= ~SP_STR_SIZE_ASCII7;   /* the bytes just changed */
  if (s->binary) h->size |= SP_STR_SIZE_BINARY;
  sp_str_lcache_drop(s->data);
}
static inline int sp_fd_grow(sp_String *s, int64_t need){
  if (need < s->cap) return 1;
  sp_gc_hdr *h = (sp_gc_hdr *)((char *)s - sizeof(sp_gc_hdr));
  int64_t new_cap = (need * 2) + 16;
  sp_str_lcache_drop(s->data);
  char *raw = (char *)realloc(sp_fd_base(s->data), SP_FD_OVH + new_cap);
  if (!raw) return 0;
  sp_gc_bytes_sub(s->cap + SP_FD_OVH); h->size -= s->cap + SP_FD_OVH;
  s->cap = new_cap; s->data = sp_fd_setup(raw); sp_fd_own(s);
  h->size += s->cap + SP_FD_OVH; sp_gc_bytes_add(s->cap + SP_FD_OVH);
  return 1;
}
static inline void sp_String_fin(void*p){sp_str_lcache_drop(((sp_String*)p)->data);free(sp_fd_base(((sp_String*)p)->data));}
/* Build a handle over an explicit byte length. Everything the caller needs to
   read out of `s` must be read BEFORE this returns: the copy below happens
   ahead of sp_gc_alloc precisely because a collection there frees an `s` that
   this stack frame is the only anchor for (roots are explicit -- the C stack is
   not scanned), so `s` may be dangling by the time it comes back. */
static inline sp_String*sp_String_new_len(const char*s,int64_t len){
  int64_t cap=(len*2)+16;
  char*raw=(char*)malloc(SP_FD_OVH+cap);
  char*data=sp_fd_setup(raw);
  memcpy(data,s,len);data[len]=0;
  sp_String*r=(sp_String*)sp_gc_alloc(sizeof(sp_String),sp_String_fin,NULL);
  r->len=len;r->cap=cap;r->data=data;r->binary=0;sp_fd_own(r);
  {sp_gc_hdr*h=(sp_gc_hdr*)((char*)r-sizeof(sp_gc_hdr));h->size+=r->cap+SP_FD_OVH;sp_gc_bytes_add(r->cap+SP_FD_OVH);}
  sp_fd_publish(r);
  return r;
}
static inline sp_String*sp_String_new(const char*s){return sp_String_new_len(s,(int64_t)strlen(s));}
/* Shared append core: `tl` is the operand byte length (strlen for the
   bare-literal-safe entry, sp_str_byte_len for the binary one). */
static inline void sp_fd_append_len(sp_String*s,const char*t,int64_t tl){if(!sp_fd_grow(s,s->len+tl))return;memcpy(s->data+s->len,t,tl);s->len+=tl;s->data[s->len]=0;sp_fd_publish(s);}
static inline void sp_String_append(sp_String*s,const char*t){if(!s||!t)return;if(sp_String_is_frozen(s)){sp_raise_frozen_str(s->data);return;}sp_fd_append_len(s,t,(int64_t)strlen(t));}
/* Binary-safe append: sizes the operand with the header length so an embedded
   NUL is preserved (Ruby String#<< / concat on a marked spinel string). */
/* Replace the buffer contents in place (the handle stays stable, so every
   alias and container holding it observes the new value; #3227). */
static inline void sp_String_set_bin(sp_String*s,const char*t){if(!s||!t)return;if(sp_String_is_frozen(s)){sp_raise_frozen_str(s->data);return;}s->len=0;sp_fd_append_len(s,t,(int64_t)sp_str_byte_len(t));}
/* the first tl bytes of t: the append form of an interpolation (emit_interp_append) */
static inline void sp_String_append_n(sp_String*s,const char*t,size_t tl){if(!s||!t)return;if(sp_String_is_frozen(s)){sp_raise_frozen_str(s->data);return;}sp_fd_append_len(s,t,(int64_t)tl);}
static inline void sp_String_append_bin(sp_String*s,const char*t){if(!s||!t)return;if(sp_String_is_frozen(s)){sp_raise_frozen_str(s->data);return;}sp_fd_append_len(s,t,(int64_t)sp_str_byte_len(t));}
/* Handle wrap for CODEGEN-emitted sources only: every spinel-emitted string
   carries a marker byte at s[-1], so the frozen state (0xf1: an explicit
   .freeze / frozen_string_literal) can be inherited safely. Runtime-internal
   callers pass bare C literals with NO marker -- they must use the plain
   sp_String_new above (reading s[-1] there is OOB and, under clang's rodata
   layout, misreads as frozen; cf. the #282 marker-probe lesson). */
static inline sp_String*sp_String_new_shared(const char*s){
  if(!s)return NULL;   /* nil into a shared-string slot stays nil (a nullable String, #4567) */
  /* Read every property of `s` HERE, before the allocation below: the handle's
     constructor can collect, and `s` is typically an unrooted temporary (the
     codegen hands this `sp_IntArray_pack(...)` directly), so touching it
     afterwards is a read of freed memory.

     The ASCII-8BIT tag is inherited, not just the frozen bit: a pack / String#b
     result captured by a block becomes one of these handles, and dropping the
     tag put its bytes back on the character-counting path. The LENGTH has to
     come with it. strlen stops at the first NUL, so sizing a binary payload
     that way truncated it: `Array.new(n, 0).pack("C*")` became the empty string
     the moment it was stored anywhere that promotes it, and the next setbyte
     raised "index 0 out of string" against a zero-length buffer. */
  int bin=sp_str_is_binary(s);
  int frozen=(((const unsigned char*)s)[-1]==0xf1);
  int64_t len=bin?(int64_t)sp_str_byte_len(s):(int64_t)strlen(s);
  sp_String*r=sp_String_new_len(s,len);
  if(bin){r->binary=1;sp_fd_publish(r);}
  if(frozen){sp_gc_hdr*h=(sp_gc_hdr*)((char*)r-sizeof(sp_gc_hdr));h->frozen=1;}
  return r;
}
static inline const char*sp_String_cstr(sp_String*s){return s->data;}
static inline int64_t sp_String_length(sp_String*s){return s->len;}

/* Cold in-place mutators (compiled once in lib/sp_string.c). */
void sp_String_prepend(sp_String*s,const char*t);
void sp_String_insert(sp_String*s,int64_t idx,const char*t);
void sp_String_replace(sp_String*s,const char*t);
sp_String*sp_String_dup(sp_String*s);
#endif /* SP_STRING_H */

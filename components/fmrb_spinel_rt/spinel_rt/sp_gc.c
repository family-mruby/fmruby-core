/* sp_gc.c -- the mark/sweep collector's non-inline machinery.
 * See sp_gc.h. The program root-marking and string-heap sweep are
 * supplied by the generated TU via sp_gc_mark_globals_hook /
 * sp_gc_str_sweep_hook. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#if !defined(__APPLE__) && !defined(__FreeBSD__)
#include <malloc.h>
#else
/* Darwin's libc has no malloc_trim; make it a no-op so call sites stay portable. */
#define malloc_trim(x) ((void)0)
#endif
#include <unistd.h>
#include "sp_gc.h"
#include "sp_marshal.h"   /* sp_marshal_vt -- the instance lives here (always linked) */

/* ---- Globals shared with the generated TU (declared extern in sp_gc.h) ---- */
SP_TLS void **sp_gc_roots[SP_GC_STACK_MAX];   /* per-worker (SP_TLS); see sp_gc.h */
SP_TLS int sp_gc_nroots = 0;
sp_gc_hdr *sp_gc_heap = NULL;
size_t sp_gc_bytes = 0;
size_t sp_gc_old_bytes = 0;
int sp_gc_cycle = 0;
void (*sp_gc_mark_suspended_fibers_hook)(void) = NULL;
void (*sp_gc_mark_globals_hook)(void) = NULL;
void (*sp_gc_str_sweep_hook)(void) = NULL;
const char *(*sp_sym_name_fn)(sp_sym) = NULL;
int (*sp_json_kind_fn)(sp_RbVal) = NULL;
mrb_int (*sp_json_len_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_json_aref_fn)(sp_RbVal, mrb_int) = NULL;
void (*sp_json_hpair_fn)(sp_RbVal, mrb_int, sp_RbVal *, sp_RbVal *) = NULL;
sp_RbVal (*sp_json_mk_hash_fn)(void) = NULL;
sp_sym (*sp_json_sym_intern_fn)(const char *) = NULL;
void (*sp_json_hash_set_fn)(sp_RbVal, const char *, sp_RbVal) = NULL;
const char *(*sp_poly_inspect_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_hash_fn)(sp_RbVal) = NULL;
const char *(*sp_obj_inspect_fn)(int cls_id, void *p) = NULL;
const char *(*sp_obj_to_s_fn)(int cls_id, void *p) = NULL;
sp_marshal_vt sp_marshal_v = {0};   /* filled by the generated TU (sp_re_init) */

/* ---- Collector-private globals ---- */
static int sp_gc_verify = 0;
static sp_gc_hdr *sp_gc_old_heap = NULL;
#define SP_GC_MARK_STACK_MAX (1024*64)
static void **sp_gc_mark_stack = NULL;
static int sp_gc_mark_top = 0;
static sp_gc_hdr **sp_gc_vsnap = NULL;
static size_t sp_gc_vsnap_n = 0, sp_gc_vsnap_cap = 0;
static size_t sp_gc_max_bytes = 0;
static int sp_gc_max_bytes_init = 0;
#define SP_GC_FULL_INTERVAL 8

/* Issue #755: bail out cleanly on OOM rather than returning NULL into a
   caller that would deref it next. */
void sp_oom_die(void){fputs("unhandled exception: out of memory\n",stderr);exit(1);}

/* ---- GC verify (SPINEL_GC_VERIFY=1): a sorted snapshot of every
 * registered header, so the scan-time membership test is O(log n). ---- */
static int sp_gc_vsnap_cmp(const void *a, const void *b){ uintptr_t x=(uintptr_t)*(sp_gc_hdr*const*)a, y=(uintptr_t)*(sp_gc_hdr*const*)b; return x<y?-1:x>y?1:0; }
static void sp_gc_vsnap_push(sp_gc_hdr *h){ if(sp_gc_vsnap_n==sp_gc_vsnap_cap){ size_t c=sp_gc_vsnap_cap?sp_gc_vsnap_cap*2:1024; sp_gc_hdr**n=(sp_gc_hdr**)realloc(sp_gc_vsnap,c*sizeof(sp_gc_hdr*)); if(!n)sp_oom_die(); sp_gc_vsnap=n; sp_gc_vsnap_cap=c; } sp_gc_vsnap[sp_gc_vsnap_n++]=h; }
static void sp_gc_verify_snapshot(void){ sp_gc_vsnap_n=0; for(sp_gc_hdr*p=sp_gc_heap;p;p=p->next)sp_gc_vsnap_push(p); for(sp_gc_hdr*p=sp_gc_old_heap;p;p=p->next)sp_gc_vsnap_push(p); if(sp_gc_vsnap_n>1)qsort(sp_gc_vsnap,sp_gc_vsnap_n,sizeof(sp_gc_hdr*),sp_gc_vsnap_cmp); }
static int sp_gc_obj_registered(sp_gc_hdr *h){ uintptr_t hv=(uintptr_t)h; size_t lo=0,hi=sp_gc_vsnap_n; while(lo<hi){ size_t m=lo+(hi-lo)/2; uintptr_t x=(uintptr_t)sp_gc_vsnap[m]; if(x==hv)return 1; if(x<hv)lo=m+1; else hi=m; } return 0; }
/* Verify diagnostics: which phase/slot the bad pointer came from. */
const char *sp_gc_dbg_phase = "?";
void *sp_gc_dbg_ctx = NULL;
static void sp_gc_verify_fail(void *obj, sp_gc_hdr *h){
  fprintf(stderr, "  [phase=%s ctx=%p]\n", sp_gc_dbg_phase, sp_gc_dbg_ctx);
  fprintf(stderr,
    "\n*** SPINEL_GC_VERIFY: collector reached a non-heap/corrupt object ***\n"
    "  obj    = %p\n  header = %p\n"
    "  This pointer is on the GC mark path but is not a registered live GC\n"
    "  allocation -- most likely a raw/aliased pointer (e.g. into a string or\n"
    "  builder buffer) reachable from a root or a scanned field. Invoking its\n"
    "  scan hook would jump through a bogus function pointer.\n",
    obj, (void*)h);
  fflush(stderr);
  fprintf(stderr, "  ->scan = %p   ->size = %zu\n\n",
    (void*)(uintptr_t)h->scan, (size_t)h->size);
  abort();
}
__attribute__((constructor)) static void sp_gc_debug_env(void){
  const char *v=getenv("SPINEL_GC_VERIFY"); sp_gc_verify=(v&&*v&&*v!='0');
}

/* Tag byte preceding `obj`: 0xfe heap-unmarked -> 0xfc; 0xfc/0xff/0xfd/0xf1
 * skipped; else a real GC object reached through its scan hook. */
void sp_gc_mark(void*obj){if(!obj)return;unsigned char pm=((unsigned char*)obj)[-1];if(pm==0xfe){((char*)obj)[-1]=(char)0xfc;return;}if(pm==0xfc||pm==0xff||pm==0xfd||pm==0xf1)return;sp_gc_hdr*h=(sp_gc_hdr*)((char*)obj-sizeof(sp_gc_hdr));if(sp_gc_verify&&!sp_gc_obj_registered(h))sp_gc_verify_fail(obj,h);if(h->marked)return;h->marked=1;if(h->scan){if(sp_gc_mark_stack&&sp_gc_mark_top<SP_GC_MARK_STACK_MAX){sp_gc_mark_stack[sp_gc_mark_top++]=obj;}else{h->scan(obj);}}}

void sp_gc_mark_all(void){if(!sp_gc_mark_stack)sp_gc_mark_stack=(void**)malloc(sizeof(void*)*SP_GC_MARK_STACK_MAX);sp_gc_mark_top=0;if(sp_gc_verify)sp_gc_verify_snapshot();int vd=sp_gc_verify;for(int i=0;i<sp_gc_nroots;i++){void**e=sp_gc_roots[i];if(vd){sp_gc_dbg_phase="root";sp_gc_dbg_ctx=(void*)e;}if((uintptr_t)e&(uintptr_t)3){sp_gc_mark_root_entry(e);}else{void*obj=*e;if(obj)sp_gc_mark(obj);}}if(vd)sp_gc_dbg_phase="fibers";if(sp_gc_mark_suspended_fibers_hook)sp_gc_mark_suspended_fibers_hook();if(vd)sp_gc_dbg_phase="globals";if(sp_gc_mark_globals_hook)sp_gc_mark_globals_hook();while(sp_gc_mark_top>0){void*obj=sp_gc_mark_stack[--sp_gc_mark_top];if(vd){sp_gc_dbg_phase="scan";sp_gc_dbg_ctx=obj;}sp_gc_hdr*h=(sp_gc_hdr*)((char*)obj-sizeof(sp_gc_hdr));if(h->scan)h->scan(obj);}if(vd){sp_gc_dbg_phase="?";sp_gc_dbg_ctx=NULL;}}

void sp_gc_collect(void){int full=(sp_gc_cycle%SP_GC_FULL_INTERVAL==0);sp_gc_cycle++;sp_gc_hdr*hh=sp_gc_old_heap;while(hh){hh->marked=0;hh=hh->next;}sp_gc_mark_all();if(full){sp_gc_hdr**pp=&sp_gc_old_heap;sp_gc_old_bytes=0;while(*pp){sp_gc_hdr*h=*pp;if(!h->marked){*pp=h->next;if(h->recycle){h->recycle(h);}else{if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));free(h);}}else{h->marked=1;sp_gc_old_bytes+=h->size;pp=&h->next;}}}else{hh=sp_gc_old_heap;while(hh){hh->marked=1;hh=hh->next;}}sp_gc_hdr**pp=&sp_gc_heap;sp_gc_bytes=sp_gc_old_bytes;while(*pp){sp_gc_hdr*h=*pp;if(!h->marked){*pp=h->next;if(h->recycle){h->recycle(h);}else{if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));free(h);}}else{h->marked=1;*pp=h->next;h->next=sp_gc_old_heap;sp_gc_old_heap=h;sp_gc_old_bytes+=h->size;sp_gc_bytes+=h->size;}}if(sp_gc_str_sweep_hook)sp_gc_str_sweep_hook();if(full)malloc_trim(0);}

/* Issue #1302: optional RSS ceiling via SPINEL_MAX_HEAP_MB; checked only
 * at GC-trigger points against real /proc/self/statm RSS. Default off. */
void sp_gc_enforce_mem_limit(void){
  if(!sp_gc_max_bytes_init){const char*e=getenv("SPINEL_MAX_HEAP_MB");long v=(e&&*e)?atol(e):0;sp_gc_max_bytes=(v>0)?(size_t)v*1024*1024:0;sp_gc_max_bytes_init=1;}
  if(!sp_gc_max_bytes)return;
#if defined(__linux__)
  FILE*sf=fopen("/proc/self/statm","r");if(!sf)return;long tot=0,res=0;int n=fscanf(sf,"%ld %ld",&tot,&res);fclose(sf);if(n!=2||res<=0)return;
  size_t rss=(size_t)res*(size_t)sysconf(_SC_PAGESIZE);
  if(rss>sp_gc_max_bytes){fprintf(stderr,"unhandled exception: out of memory (RSS %zu MB exceeded SPINEL_MAX_HEAP_MB=%zu MB)\n",rss/(1024*1024),sp_gc_max_bytes/(1024*1024));exit(1);}
#endif
}

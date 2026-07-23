/* sp_alloc.c -- the single, shared definitions backing sp_alloc.h.

   Owns the string heap so that both the generated program and every standalone
   lib/*.c allocate onto one heap. sp_str_sweep is registered with the object GC
   via a constructor, so a collection triggered from any TU also reaps strings. */
#include "sp_alloc.h"

sp_str_hdr *sp_str_heap = NULL;
size_t sp_str_heap_bytes = 0;
size_t sp_str_threshold = 256 * 1024;
size_t sp_str_threshold_init = 256 * 1024;
int sp_str_stress_checked = 0;

const char sp_str_empty_data[] = "\xff";

/* Object-heap collection threshold (was per-TU static in sp_runtime.h; now
   shared so sp_gc_alloc can live in sp_alloc.h and lib TUs allocate too). */
size_t sp_gc_threshold = 256 * 1024;
size_t sp_gc_threshold_init = 256 * 1024;
int sp_gc_stress_checked = 0;

#ifdef SP_THREADS
pthread_mutex_t sp_heap_lock = PTHREAD_MUTEX_INITIALIZER;   /* see sp_alloc.h */
#endif

/* Re-tune the object / string GC thresholds from the pre-collect live bytes
   (the heuristic mirrors the original inline code in sp_gc_alloc / sp_str_alloc). */
static void sp_gc_retune_object(size_t before) {
  size_t freed = before - sp_gc_bytes;
  if (freed < before / 4) { sp_gc_threshold = before * 2; }
  else if (sp_gc_bytes > 0) { sp_gc_threshold = sp_gc_bytes * 4; if (sp_gc_threshold < sp_gc_threshold_init) sp_gc_threshold = sp_gc_threshold_init; }
  else { sp_gc_threshold = sp_gc_threshold_init; }
}
static void sp_str_retune(size_t before) {
  size_t freed = before - sp_str_heap_bytes;
  if (freed < before / 4) { sp_str_threshold = before * 2; }
  else if (sp_str_heap_bytes > 0) { sp_str_threshold = sp_str_heap_bytes * 4; if (sp_str_threshold < sp_str_threshold_init) sp_str_threshold = sp_str_threshold_init; }
  else { sp_str_threshold = sp_str_threshold_init; }
}

/* Collect and re-tune. The caller guarantees exclusive heap access: the
   single-threaded allocators hold sp_heap_lock; the threaded build runs the
   _all variant under stop-the-world (every other worker parked), via
   sp_stw_collect, so neither heap is mutated during the sweep. The object and
   string variants retune only their own threshold, matching the original
   per-heap inline collection so the single-threaded path stays byte-identical;
   _all retunes both since one stop-the-world sweeps both heaps. */
void sp_gc_collect_retune(void) {
  size_t before = sp_gc_bytes;
  sp_gc_collect();
  sp_gc_retune_object(before);
  sp_gc_enforce_mem_limit();
}
void sp_str_collect_retune(void) {
  size_t before = sp_str_heap_bytes;
  sp_gc_collect();
  sp_str_retune(before);
}
void sp_gc_collect_retune_all(void) {
  size_t ob = sp_gc_bytes, sb = sp_str_heap_bytes;
  sp_gc_collect();
  sp_gc_retune_object(ob);
  sp_str_retune(sb);
  sp_gc_enforce_mem_limit();
}
/* Either heap over its trigger? Used by sp_stw_collect to skip a redundant
   stop-the-world when another worker just collected. */
int sp_gc_collection_wanted(void) {
  /* Everything read atomically: this runs before the world is stopped
     (sp_stw_collect's early-out), concurrent with other workers' relaxed
     counter adds AND with the allocators' one-shot GC-stress threshold
     write (heap-locked, but this reader holds only the sched lock). The
     retune writes are plain but never overlap: they run while g_stw_active
     is set, and this is only called with it clear, under the same lock
     that publishes it. A stale read at worst skips one redundant
     collection. */
  return SP_GC_CTR_GET(sp_gc_bytes) > SP_GC_CTR_GET(sp_gc_threshold) ||
         SP_GC_CTR_GET(sp_str_heap_bytes) > SP_GC_CTR_GET(sp_str_threshold);
}

void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *)) {
  SP_HEAP_LOCK();
  /* The threshold store is atomic: sp_gc_collection_wanted reads it without
     the heap lock. threshold_init stays plain -- only retune reads it, under
     stop-the-world, ordered after this by the writer's park. */
  if (!sp_gc_stress_checked) { sp_gc_stress_checked = 1; const char *e = getenv("SPINEL_GC_STRESS"); if (e && *e && *e != '0') { SP_GC_CTR_SET(sp_gc_threshold, 2048); sp_gc_threshold_init = 2048; } }
  if (SP_GC_CTR_GET(sp_gc_bytes) > sp_gc_threshold) {
#ifdef SP_THREADS
    /* Drop the heap lock before stopping the world: a worker stalled here on the
       heap lock could never reach a safepoint, deadlocking the collector. */
    SP_HEAP_UNLOCK();
    sp_stw_collect();
    SP_HEAP_LOCK();
#else
    sp_gc_collect_retune();
#endif
  }
  size_t need = sizeof(sp_gc_hdr) + sz;
  sp_gc_hdr *h = (sp_gc_hdr *)calloc(1, need);
  if (!h) sp_oom_die();
  h->finalize = fin; h->scan = scn; h->size = need; h->marked = 0;
  /* CAS push, not a plain locked store: the pool-hit relink pushes onto the
     same list head WITHOUT the heap lock, so every writer must use the same
     atomic protocol (sp_gc.h). */
  SP_GC_HEAP_PUSH(h); SP_GC_CTR_ADD(sp_gc_bytes, need);
  SP_HEAP_UNLOCK();
  return (char *)h + sizeof(sp_gc_hdr);
}
void *sp_gc_alloc_nogc(size_t sz, void (*fin)(void *), void (*scn)(void *)) {
  size_t need = sizeof(sp_gc_hdr) + sz;
  sp_gc_hdr *h = (sp_gc_hdr *)calloc(1, need);
  if (!h) sp_oom_die();
  h->finalize = fin; h->scan = scn; h->size = need; h->marked = 0;
  SP_HEAP_LOCK();
  SP_GC_HEAP_PUSH(h); SP_GC_CTR_ADD(sp_gc_bytes, need);
  SP_HEAP_UNLOCK();
  return (char *)h + sizeof(sp_gc_hdr);
}

SP_TLS struct sp_str_lcache_entry sp_str_lcache[SP_STR_LCACHE_SIZE];

void sp_str_lcache_clear(void) {
  for (unsigned i = 0; i < SP_STR_LCACHE_SIZE; i++) sp_str_lcache[i].s = NULL;
}

/* sp_mark_string (sp_gc.h) flips a live string's marker 0xfe->0xfc during the
   mark phase; sweep keeps the marked ones and frees the rest. A frozen heap
   string (0xf1) is kept across sweeps (a live frozen global must survive, and
   frozen literals are immortal). */
void sp_str_sweep(void) {
  sp_str_hdr **pp = &sp_str_heap;
  while (*pp) {
    sp_str_hdr *h = *pp;
    char *body = (char *)(h + 1);
    if ((unsigned char)body[0] == 0xfc) {
      body[0] = (char)0xfe;
      pp = &h->next;
    }
    else if ((unsigned char)body[0] == 0xf1) {
      pp = &h->next;
    }
    else {
      *pp = h->next;
      sp_str_heap_bytes -= h->size;   /* keep the string-heap live-byte count in step */
      free(h);
    }
  }
  sp_str_lcache_clear();
}

/* Wire string sweep into the object collector. Runs before main, so the hook is
   set before the first allocation can trigger a collection. */
__attribute__((constructor)) static void sp_alloc_install_hooks(void) {
  sp_gc_str_sweep_hook = sp_str_sweep;
}

/* Float#to_s / #inspect (declared in sp_alloc.h): shortest round-trip decimal.
   Moved out-of-line from the header -- cold (display only) and large. */
const char *sp_float_to_s(mrb_float f) {
  if(f!=f){char*r=sp_str_alloc_raw(4);r[0]='N';r[1]='a';r[2]='N';r[3]=0;return r;}
  if(f==HUGE_VAL||f==-HUGE_VAL){if(f<0){char*r=sp_str_alloc_raw(10);memcpy(r,"-Infinity",10);return r;}char*r=sp_str_alloc_raw(9);memcpy(r,"Infinity",9);return r;}
  if(f==0.0){if(signbit(f)){char*r=sp_str_alloc_raw(5);memcpy(r,"-0.0",5);return r;}char*r=sp_str_alloc_raw(4);memcpy(r,"0.0",4);return r;}
  /* integer-valued doubles skip the shortest-representation search below
     (up to 18 snprintf+strtod probes): write the integer digits + ".0"
     directly. |f| < 2^53 keeps the mrb_int cast exact. */
  if(f>-1e15&&f<1e15&&f==(mrb_float)(mrb_int)f){
    /* bound matches the fixed-notation window below (decimal exponent <= 15);
       larger integer-valued doubles print scientific like CRuby */
    char*r=sp_str_alloc_raw(32);
    char*e=sp_w_int(r,(mrb_int)f);
    *e++='.';*e++='0';*e=0;
    sp_str_set_len(r,(size_t)(e-r));
    return r;
  }
  char tmp[64];int p;
  for(p=0;p<=17;p++){snprintf(tmp,sizeof(tmp),"%.*e",p,(double)f);if(strtod(tmp,NULL)==f)break;}
  int neg=(tmp[0]=='-')?1:0;const char*s=tmp+neg;char digits[32];int dlen=0;
  digits[dlen++]=*s++;
  if(*s=='.'){s++;while(*s&&*s!='e'&&*s!='E')digits[dlen++]=*s++;}
  while(*s&&*s!='e'&&*s!='E')s++;
  int exp_val=(*s)?atoi(s+1):0;int decpt=exp_val+1;
  char*out=sp_str_alloc_raw(64);int o=0;
  if(neg)out[o++]='-';
  /* fixed notation when the point sits within the digits (a fractional part,
     dlen>decpt) OR the integer part is <= 15 digits; a longer integer-valued
     value (dlen<=decpt, decpt>15) prints scientific like CRuby (#2593). */
  if(decpt>0&&(decpt<=15||dlen>decpt)){
    if(decpt<dlen){memcpy(out+o,digits,decpt);o+=decpt;out[o++]='.';memcpy(out+o,digits+decpt,dlen-decpt);o+=(dlen-decpt);}
    else{memcpy(out+o,digits,dlen);o+=dlen;for(int i=dlen;i<decpt;i++)out[o++]='0';out[o++]='.';out[o++]='0';}
  }else if(decpt<=0&&decpt>-4){
    out[o++]='0';out[o++]='.';for(int i=decpt;i<0;i++)out[o++]='0';memcpy(out+o,digits,dlen);o+=dlen;
  }else{
    out[o++]=digits[0];out[o++]='.';
    if(dlen==1)out[o++]='0';else{memcpy(out+o,digits+1,dlen-1);o+=(dlen-1);}
    out[o++]='e';int e=decpt-1;
    if(e>=0)out[o++]='+';else{out[o++]='-';e=-e;}
    if(e<10){out[o++]='0';out[o++]=(char)('0'+e);}else o+=snprintf(out+o,16,"%d",e);
  }
  out[o]=0;sp_str_set_len(out,(size_t)o);return out;
}

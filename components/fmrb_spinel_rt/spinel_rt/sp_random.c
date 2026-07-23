/* sp_random.c -- the shared Kernel-level PRNG stream and the Random
   instance methods (see sp_random.h). */
#include <time.h>
#include <string.h>
#include "sp_random.h"
#include "sp_alloc.h"   /* sp_str_alloc / sp_str_set_len / sp_float_to_s / sp_raise_cls / sp_gc_alloc */
#include "sp_format.h"  /* sp_Range_inspect */
#include "sp_str.h"     /* sp_sprintf (defined in the generated TU) */

/* PCG-XSH-RR constants (64-bit state, 32-bit output). */
#define SP_PCG_MULT 6364136223846793005ULL
#define SP_PCG_INC  1442695040888963407ULL

uint32_t sp_pcg32_adv(uint64_t *state) {
  uint64_t old = *state;
  *state = old * SP_PCG_MULT + SP_PCG_INC;
  /* output: xorshift-high folds the state, then a random rotate */
  uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
  uint32_t rot = (uint32_t)(old >> 59u);
  return (xs >> rot) | (xs << ((32 - rot) & 31));
}

/* The PCG init sequence: zero the state, step, add the seed, then mix.
   The extra mixing rounds keep nearby seeds from producing correlated
   early output. */
void sp_pcg_seed(uint64_t *state, uint64_t seed) {
  *state = 0;
  (void)sp_pcg32_adv(state);
  *state += seed;
  for (int i = 0; i < 10; i++) (void)sp_pcg32_adv(state);
}

/* Per-worker (SP_TLS) in the threaded build: the generator has no
   internal lock, so a shared stream would race across workers. */
static SP_TLS uint64_t sp_krand_state;
static SP_TLS int sp_krand_seeded;

void sp_krand_srand(uint64_t seed) {
  sp_pcg_seed(&sp_krand_state, seed);
  sp_krand_seeded = 1;
}

uint64_t sp_krand_next(void) {
  if (!sp_krand_seeded) {
    /* Auto-seed on first draw, mirroring CRuby's startup seeding so
       unseeded rand/shuffle/sample vary per run. time() alone repeats
       within a second; mix in clock() and the per-worker state address. */
    sp_krand_srand((uint64_t)time(NULL) ^ ((uint64_t)clock() << 24) ^
                   (uint64_t)(uintptr_t)&sp_krand_state);
  }
  uint64_t hi = sp_pcg32_adv(&sp_krand_state);
  return (hi << 32) | sp_pcg32_adv(&sp_krand_state);
}

mrb_int sp_krand_below(mrb_int n) {
  if (n <= 0) return 0;
  /* uniform in [0, n) without modulo bias: reject draws below the
     wrap-around threshold (0 for powers of two, so the loop is rare) */
  uint64_t umax = (uint64_t)n;
  uint64_t threshold = (uint64_t)(-(int64_t)umax) % umax;
  uint64_t r;
  do {
    r = sp_krand_next();
  } while (r < threshold);
  return (mrb_int)(r % umax);
}

mrb_float sp_krand_float(void) {
  return (mrb_float)(sp_krand_next() >> 11) / (mrb_float)(1ULL << 53);
}

/* ---- Random instance methods ---- */

SP_TLS sp_Random sp_random_default;
uint64_t sp_random_next(sp_Random *r) {
  if (r == &sp_random_default) return sp_krand_next();
  uint64_t hi = sp_pcg32_adv(&r->state);
  return (hi << 32) | sp_pcg32_adv(&r->state);
}
sp_Random *sp_Random_new(mrb_int seed) {
  sp_Random *r = (sp_Random *)sp_gc_alloc(sizeof(sp_Random), NULL, NULL);
  sp_pcg_seed(&r->state, (uint64_t)seed);
  r->seed = seed;
  return r;
}
/* Random.new(Float): CRuby truncates the float to an integer seed. A plain
   (mrb_int) cast is UB when the truncated value is out of range, so seed from
   the in-range truncation when it fits and from the raw float bits otherwise
   -- always deterministic (same float -> same stream), which is the actual
   contract (the exact sequence is not MT19937 anyway). */
sp_Random *sp_Random_new_float(mrb_float f) {
  uint64_t s;
  if (f >= -9.2233720368547758e18 && f < 9.2233720368547758e18) {
    s = (uint64_t)(int64_t)f;
  } else {
    uint64_t bits; memcpy(&bits, &f, sizeof bits);
    s = bits;
  }
  sp_Random *r = (sp_Random *)sp_gc_alloc(sizeof(sp_Random), NULL, NULL);
  sp_pcg_seed(&r->state, s);
  r->seed = (mrb_int)s;
  return r;
}
/* Random#seed: the seed the instance was constructed from. */
mrb_int sp_Random_seed(sp_Random *r) { return r ? r->seed : 0; }
/* Random#== compares by internal state (two same-seed, same-position instances
   are equal; advancing one makes them differ). */
mrb_bool sp_Random_eq(sp_Random *a, sp_Random *b) {
  if (a == b) return TRUE;
  if (!a || !b) return FALSE;
  return (a->state == b->state && a->seed == b->seed) ? TRUE : FALSE;
}
/* Random.new with no seed: OS-entropy-ish auto seed. time() alone made two
   instances created in the same second identical; mix in a per-thread counter
   and the object address so every instance gets its own stream. */
sp_Random *sp_Random_new_auto(void) {
  static SP_TLS uint64_t sp_random_auto_ctr = 0;
  sp_Random *r = (sp_Random *)sp_gc_alloc(sizeof(sp_Random), NULL, NULL);
  uint64_t e = (((uint64_t)time(NULL)) << 20) ^ (++sp_random_auto_ctr * 0x9E3779B97F4A7C15ULL) ^
               (uint64_t)(uintptr_t)r;
  sp_pcg_seed(&r->state, e);
  r->seed = (mrb_int)(e >> 1);
  return r;
}
/* Random#rand(Range): an integer in the (int-endpoint) range, empty raises. */
mrb_int sp_Random_rand_range(sp_Random *r, sp_Range rg) {
  mrb_int lo = rg.first, hi = rg.excl ? rg.last - 1 : rg.last;
  if (hi < lo) sp_raise_cls("ArgumentError", sp_sprintf("invalid argument - %s", sp_Range_inspect(&rg)));
  if (!r) return lo;
  return lo + (mrb_int)(sp_random_next(r) % ((uint64_t)(hi - lo) + 1));
}
mrb_int sp_Random_rand_int(sp_Random *r, mrb_int n) {
  if (n <= 0) sp_raise_cls("ArgumentError", sp_sprintf("invalid argument - %lld", (long long)n));
  if (!r) return 0;
  return (mrb_int)(sp_random_next(r) % (uint64_t)n);
}
mrb_float sp_Random_rand_float(sp_Random *r) {
  if (!r) return 0.0;
  return (mrb_float)(sp_random_next(r) >> 11) / (mrb_float)(1ULL << 53);
}
/* Random#rand(Float bound): a random Float in [0, bound). A non-positive bound
   raises ArgumentError like the Integer form (MRI validates both). */
mrb_float sp_Random_rand_float_bound(sp_Random *r, mrb_float bound) {
  if (bound <= 0) sp_raise_cls("ArgumentError", sp_sprintf("invalid argument - %s", sp_float_to_s(bound)));
  return sp_Random_rand_float(r) * bound;
}
/* Class-method forms (`Random.rand` / `Random.bytes`) share the default
   instance, mirroring CRuby's Random::DEFAULT. Its draws redirect to the
   shared Kernel stream (see sp_random_next), which lazily self-seeds and
   is per-worker in the threaded build. */
sp_Random *sp_random_default_get(void) {
  return &sp_random_default;
}
/* Random#bytes(n) — n random bytes as a String. Uses sp_str_set_len
   so embedded NULs are preserved and #length reports n. */
const char *sp_Random_bytes(sp_Random *r, mrb_int n) {
  if (n < 0) n = 0;
  char *b = sp_str_alloc((size_t)n);
  for (mrb_int i = 0; i < n; i++) b[i] = (char)(sp_random_next(r) & 0xff);
  b[n] = 0;
  sp_str_set_len(b, (size_t)n);
  return b;
}
/* Random#rand(Float range): a Float in [lo, hi) (or [lo, hi] for an inclusive
   range, though the float boundary is effectively open). */
mrb_float sp_Random_rand_float_range(sp_Random *r, mrb_float lo, mrb_float hi) {
  return lo + sp_Random_rand_float(r) * (hi - lo);
}
/* Random.new_seed: a fresh nonneg seed drawn from the default stream. */
mrb_int sp_Random_new_seed(void) {
  mrb_int s = (mrb_int)(sp_random_next(sp_random_default_get()) >> 1);
  return s < 0 ? -s : s;
}
/* Random.urandom(n): n random bytes as a String (spinel has no real OS entropy
   source wired up, so this draws from a freshly time-seeded stream). */
const char *sp_Random_urandom(mrb_int n) {
  if (n < 0) sp_raise_cls("ArgumentError", "negative string size");
  sp_Random tmp; tmp.seed = 0;
  sp_pcg_seed(&tmp.state, ((uint64_t)time(NULL) << 20) ^ (uint64_t)(uintptr_t)&tmp ^ 0x9E3779B97F4A7C15ULL);
  char *b = sp_str_alloc((size_t)n);
  for (mrb_int i = 0; i < n; i++) b[i] = (char)(sp_random_next(&tmp) & 0xff);
  b[n] = 0;
  sp_str_set_len(b, (size_t)n);
  return b;
}
/* Random#inspect / #to_s: CRuby's default object rendering (the seed is not
   part of it; the address matches CRuby's zero-padded 16-digit form). */
const char *sp_Random_inspect(sp_Random *r) {
  return sp_sprintf("#<Random:0x%016llx>", (unsigned long long)(uintptr_t)r);
}
/* Kernel#srand: seed the shared Kernel stream and remember the previous
   seed, which srand returns (CRuby returns the prior seed, not the new
   one). Every rand form -- bare/int/range, shuffle, sample, the Random
   default instance -- draws from that one stream, so a single srand makes
   them all reproducible. */
static SP_TLS mrb_int sp_kernel_seed = 0;
mrb_int sp_kernel_srand(mrb_int seed) {
  mrb_int prev = sp_kernel_seed;
  sp_kernel_seed = seed;
  sp_krand_srand((uint64_t)seed);
  return prev;
}

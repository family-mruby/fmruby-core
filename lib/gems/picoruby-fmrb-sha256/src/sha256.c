/*
 * Digest::SHA256 for the builds without mbedTLS (the browser build).
 *
 * The same small API as CRuby's, as much of it as Asterism::Transfer uses:
 *
 *   d = Digest::SHA256.new
 *   d.update(bytes)        -> self (also <<)
 *   d.hexdigest            -> 64 hex digits of what was given so far
 *   d.digest               -> the 32 bytes
 *   Digest::SHA256.hexdigest(bytes)
 *
 * FIPS 180-4, compression one 64-byte block at a time; hexdigest and digest
 * work on a copy of the state, so update may go on after them.
 */
#include <string.h>
#include <stdint.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>

typedef struct {
  uint32_t h[8];
  uint64_t len;       /* bytes given */
  uint8_t buf[64];
  uint32_t fill;      /* bytes in buf */
} sha256_ctx;

static const uint32_t K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_init(sha256_ctx *c)
{
  static const uint32_t iv[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };
  memcpy(c->h, iv, sizeof(iv));
  c->len = 0;
  c->fill = 0;
}

static void sha256_block(uint32_t h[8], const uint8_t *p)
{
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
           ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

static void sha256_update(sha256_ctx *c, const uint8_t *p, size_t n)
{
  c->len += n;
  if (c->fill > 0) {
    size_t take = 64 - c->fill;
    if (take > n) take = n;
    memcpy(c->buf + c->fill, p, take);
    c->fill += (uint32_t)take;
    p += take;
    n -= take;
    if (c->fill < 64) return;
    sha256_block(c->h, c->buf);
    c->fill = 0;
  }
  while (n >= 64) {
    sha256_block(c->h, p);
    p += 64;
    n -= 64;
  }
  if (n > 0) {
    memcpy(c->buf, p, n);
    c->fill = (uint32_t)n;
  }
}

/* The digest of what was given, the context left as it was. */
static void sha256_final(const sha256_ctx *src, uint8_t out[32])
{
  sha256_ctx c = *src;
  uint64_t bits = c.len * 8;
  uint8_t pad[72];
  size_t padn = (c.fill < 56) ? 56 - c.fill : 120 - c.fill;
  memset(pad, 0, sizeof(pad));
  pad[0] = 0x80;
  for (int i = 0; i < 8; i++) {
    pad[padn + i] = (uint8_t)(bits >> (56 - 8 * i));
  }
  sha256_update(&c, pad, padn + 8);
  for (int i = 0; i < 8; i++) {
    out[i * 4] = (uint8_t)(c.h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(c.h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(c.h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)c.h[i];
  }
}

static void ctx_free(mrb_state *mrb, void *p)
{
  mrb_free(mrb, p);
}

static const struct mrb_data_type sha256_type = { "Digest::SHA256", ctx_free };

static sha256_ctx *get_ctx(mrb_state *mrb, mrb_value self)
{
  return (sha256_ctx *)mrb_data_get_ptr(mrb, self, &sha256_type);
}

static mrb_value m_initialize(mrb_state *mrb, mrb_value self)
{
  sha256_ctx *c = (sha256_ctx *)DATA_PTR(self);
  if (c == NULL) {
    c = (sha256_ctx *)mrb_malloc(mrb, sizeof(sha256_ctx));
    DATA_PTR(self) = c;
    DATA_TYPE(self) = &sha256_type;
  }
  sha256_init(c);
  return self;
}

static mrb_value m_update(mrb_state *mrb, mrb_value self)
{
  mrb_value s;
  mrb_get_args(mrb, "S", &s);
  sha256_update(get_ctx(mrb, self), (const uint8_t *)RSTRING_PTR(s), (size_t)RSTRING_LEN(s));
  return self;
}

static mrb_value m_digest(mrb_state *mrb, mrb_value self)
{
  uint8_t out[32];
  sha256_final(get_ctx(mrb, self), out);
  return mrb_str_new(mrb, (const char *)out, 32);
}

static mrb_value hex_of(mrb_state *mrb, const uint8_t out[32])
{
  static const char hx[] = "0123456789abcdef";
  char s[64];
  for (int i = 0; i < 32; i++) {
    s[i * 2] = hx[out[i] >> 4];
    s[i * 2 + 1] = hx[out[i] & 15];
  }
  return mrb_str_new(mrb, s, 64);
}

static mrb_value m_hexdigest(mrb_state *mrb, mrb_value self)
{
  uint8_t out[32];
  sha256_final(get_ctx(mrb, self), out);
  return hex_of(mrb, out);
}

static mrb_value m_s_hexdigest(mrb_state *mrb, mrb_value klass)
{
  mrb_value s;
  mrb_get_args(mrb, "S", &s);
  sha256_ctx c;
  uint8_t out[32];
  sha256_init(&c);
  sha256_update(&c, (const uint8_t *)RSTRING_PTR(s), (size_t)RSTRING_LEN(s));
  sha256_final(&c, out);
  return hex_of(mrb, out);
}

void
mrb_picoruby_fmrb_sha256_gem_init(mrb_state *mrb)
{
  struct RClass *digest = mrb_define_module(mrb, "Digest");
  struct RClass *cls = mrb_define_class_under(mrb, digest, "SHA256", mrb->object_class);
  MRB_SET_INSTANCE_TT(cls, MRB_TT_CDATA);
  mrb_define_method(mrb, cls, "initialize", m_initialize, MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "update", m_update, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, cls, "<<", m_update, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, cls, "digest", m_digest, MRB_ARGS_NONE());
  mrb_define_method(mrb, cls, "hexdigest", m_hexdigest, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, cls, "hexdigest", m_s_hexdigest, MRB_ARGS_REQ(1));
}

void
mrb_picoruby_fmrb_sha256_gem_final(mrb_state *mrb)
{
  (void)mrb;
}

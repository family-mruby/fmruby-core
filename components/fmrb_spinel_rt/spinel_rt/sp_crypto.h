/* sp_crypto.h -- Compact SHA-256 / HMAC / PBKDF2 / Base64URL / CSPRNG
 *
 * Pure C, no spinel-runtime dependency. Intended for spinel programs
 * that need a small in-tree crypto surface without dragging in
 * OpenSSL or libsodium. Sibling to sp_bigint.{h,c} in scope: a
 * vendored, audit-sized C helper that ships with spinel so apps
 * don't each reinvent it.
 *
 * The seven functions below are the canonical surface tep ended up
 * with after a year of building on the same primitives -- if you
 * find yourself wanting a different shape, please file an issue
 * before adding to this list. The point is to keep the surface
 * small enough to read in one sitting.
 *
 * Naming
 * ------
 * All exported symbols use the `sp_crypto_` prefix (matches the
 * `sp_bigint_` convention). State buffers are per-function statics
 * -- the next call to the same function clobbers the buffer, so
 * copy on the caller side if the value must outlive the next call.
 *
 * Inputs
 * ------
 * String inputs are NUL-terminated. Length is taken via strlen()
 * inside each function. For binary-safe inputs (containing embedded
 * NULs), use the explicit-length internal API exposed by including
 * this header in your own .c file -- see sp_crypto.c for the
 * declarations.
 *
 * Thread safety
 * -------------
 * NOT thread-safe -- the static return buffers are shared. Spinel
 * apps that fork (the canonical concurrency model) are fine; apps
 * that thread need to caller-side serialize.
 */
#ifndef SP_CRYPTO_H
#define SP_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-1(msg) -> 40-char lowercase hex. Legacy hash, kept for
 * WebSocket handshake (RFC 6455 §1.3 requires SHA-1). Do NOT
 * use for new security designs; SHA-256 is the right primitive. */
const char *sp_crypto_sha1_hex(const char *msg);

/* SHA-256(msg) -> 64-char lowercase hex. */
const char *sp_crypto_sha256_hex(const char *msg);

/* Sec-WebSocket-Accept = base64(SHA-1(client_key + GUID)) per
 * RFC 6455 §1.3. Returns a 28-char string ending in `=`. The
 * only modern use case for SHA-1 in this codebase; sugars the
 * concat+sha1+base64 dance into one call. */
const char *sp_crypto_websocket_accept(const char *client_key);

/* HMAC-SHA256(key, msg) -> 64-char lowercase hex. */
const char *sp_crypto_hmac_sha256_hex(const char *key, const char *msg);

/* HMAC-SHA256(key, msg) -> 43-char unpadded base64url. */
const char *sp_crypto_hmac_sha256_b64url(const char *key, const char *msg);

/* Base64URL (RFC 4648 §5, no padding) encode/decode. Max input
 * length ~12 KiB (encode) / ~16 KiB (decode), bump the buffer in
 * sp_crypto.c if your callers need more. */
const char *sp_crypto_b64url_encode(const char *src);
const char *sp_crypto_b64url_decode(const char *src);

/* PBKDF2-HMAC-SHA256(password, salt, iters) -> 43-char unpadded
 * base64url (32 bytes derived; dkLen > 32 not supported). */
const char *sp_crypto_pbkdf2_sha256_b64url(const char *password, const char *salt, int iters);

/* CSPRNG: nbytes random bytes (clamped to [1, 64]) as unpadded
 * base64url. Uses arc4random_buf on BSD/macOS; on Linux/POSIX
 * getrandom(2), then /dev/urandom. Returns NULL when no secure source
 * is available (fails closed -- never degrades to weak randomness). */
const char *sp_crypto_random_b64url(int nbytes);

#ifdef __cplusplus
}
#endif

#endif /* SP_CRYPTO_H */

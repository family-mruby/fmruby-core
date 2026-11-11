/*
 * mbedtls_tls13.h - what the native program adds to picoruby-mbedtls's Mbed
 * TLS configuration (doc/linux_native/report/l19.md).
 *
 * picoruby-mbedtls compiles Mbed TLS 3.6 into libmruby with its own
 * include/mbedtls_config.h: TLS 1.2 only, no PSA. A zenohd that asks for
 * mutual TLS accepts TLS 1.3 only (zenoh-link-tls fixes the version), so the
 * Zenoh TLS link (picoruby-asterism-zenoh's ports/posix/zp_tls_posix.c)
 * needs TLS 1.3, which in Mbed TLS 3.6 runs on PSA. Included after that file
 * through MBEDTLS_USER_CONFIG_FILE, which lib/add/family_mruby_native.rb
 * defines for the whole native libmruby (every object that reads an Mbed
 * TLS header sees the same configuration). The simulation and the browser
 * build do not read this file.
 *
 * The library's own TLS 1.2 stays as it was. PSA is added without its key
 * storage; the legacy digest and cipher modules stay the ones MbedTLS::Digest
 * and friends use. Certificate dates are still not checked
 * (MBEDTLS_HAVE_TIME_DATE stays off, as on ESP32).
 */
#ifndef FMRB_NATIVE_MBEDTLS_TLS13_H
#define FMRB_NATIVE_MBEDTLS_TLS13_H

#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_HKDF_C
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
/* RSA-PSS: the signatures TLS 1.3 uses with an RSA certificate (the
 * console's are ECDSA P-256; a router with RSA would need these). */
#define MBEDTLS_PKCS1_V21

#endif /* FMRB_NATIVE_MBEDTLS_TLS13_H */

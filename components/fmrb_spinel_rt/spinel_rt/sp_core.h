/* sp_core.h -- runtime helpers split out of sp_runtime.h into
 * libspinel_rt.a so they are compiled once rather than re-parsed and
 * re-codegen'd in every generated translation unit.
 *
 * Signatures use intptr_t / double directly (== mrb_int / mrb_float)
 * to stay decoupled from the runtime's typedefs. These helpers call
 * sp_raise_cls / sp_sprintf, which live in the generated TU
 * (sp_runtime.h) and resolve at link time.
 */
#ifndef SP_CORE_H
#define SP_CORE_H

#include <stdint.h>

/* String -> number parsers (cold, I/O-boundary). */
intptr_t sp_str_to_i_cruby(const char *s);
double sp_str_to_f_cruby(const char *s);
intptr_t sp_str_to_i_base(const char *s, intptr_t base);
intptr_t sp_str_to_i_strict(const char *s);
intptr_t sp_str_to_i_strict_base(const char *s, intptr_t base);
double  sp_str_to_f_strict(const char *s);

/* Cold integer-math + String#oct helpers. */
intptr_t sp_gcd(intptr_t a, intptr_t b);
intptr_t sp_lcm(intptr_t a, intptr_t b);
intptr_t sp_powmod(intptr_t base, intptr_t exp, intptr_t mod);
intptr_t sp_ceildiv(intptr_t a, intptr_t b);
intptr_t sp_int_clamp(intptr_t v, intptr_t lo, intptr_t hi);
double sp_float_clamp(double v, double lo, double hi);
intptr_t sp_int_sqrt(intptr_t n);
intptr_t sp_ipow10(intptr_t p);
intptr_t sp_int_round(intptr_t v, intptr_t nd);
intptr_t sp_int_ceil(intptr_t v, intptr_t nd);
intptr_t sp_int_floor(intptr_t v, intptr_t nd);
intptr_t sp_int_truncate(intptr_t v, intptr_t nd);
intptr_t sp_str_oct(const char *s);

#endif

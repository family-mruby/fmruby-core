#ifndef SP_FORMAT_H
#define SP_FORMAT_H
/* sp_format.h -- cold value-type display helpers, split out of sp_runtime.h
   into libspinel_rt.a.

   Each formats a small value type (Complex / Rational / Range) into a freshly
   GC-allocated string. They need only the shared types (sp_types.h) and the
   shared string allocator (sp_alloc.h), and are only ever reached on a cold
   inspect/to_s path, so they compile once in the archive instead of being
   re-parsed and re-codegen'd in every generated translation unit. */
#include "sp_types.h"   /* sp_Complex, sp_Rational, sp_Range */
#include "sp_time.h"    /* sp_Time */

const char *sp_complex_inspect(sp_Complex c);
const char *sp_complex_to_s(sp_Complex c);
const char *sp_rational_inspect(sp_Rational r);
const char *sp_rational_to_s(sp_Rational r);
const char *sp_Range_inspect(sp_Range *r);
const char *sp_Time_inspect(sp_Time *t);
const char *sp_Time_to_s(sp_Time *t);

/* Value-type arithmetic (cold: only reached when a program actually uses
   Complex / Rational; optcarrot touches Complex only under --nestopia-palette).
   Emitted by codegen via the sp_complex_%s / sp_rational_%s operator dispatch. */
sp_Complex sp_complex_polar(mrb_float m, mrb_float a, int m_is_f);
sp_Complex sp_complex_add(sp_Complex a, sp_Complex b);
sp_Complex sp_complex_sub(sp_Complex a, sp_Complex b);
sp_Complex sp_complex_mul(sp_Complex a, sp_Complex b);
sp_Complex sp_complex_div(sp_Complex a, sp_Complex b);
sp_Complex sp_complex_div_real(sp_Complex a, mrb_float b);
sp_Complex sp_complex_div_int(sp_Complex a, mrb_int b);
sp_Complex sp_complex_neg(sp_Complex a);
sp_Complex sp_complex_conjugate(sp_Complex a);
sp_Complex sp_complex_pow(sp_Complex a, mrb_int e);
sp_Complex sp_complex_pow_c(sp_Complex z, sp_Complex w);   /* z ** w (general) */
mrb_float sp_complex_abs(sp_Complex a);
mrb_float sp_complex_abs2(sp_Complex a);
mrb_bool sp_complex_eq(sp_Complex a, sp_Complex b);

sp_Rational sp_rational_new(mrb_int n, mrb_int d);
sp_Rational sp_str_to_r(const char *s);
sp_Rational sp_rational_add(sp_Rational a, sp_Rational b);
sp_Rational sp_rational_sub(sp_Rational a, sp_Rational b);
sp_Rational sp_rational_mul(sp_Rational a, sp_Rational b);
sp_Rational sp_rational_div(sp_Rational a, sp_Rational b);
sp_Rational sp_rational_neg(sp_Rational a);
sp_Rational sp_rational_abs(sp_Rational a);
sp_Rational sp_rational_pow(sp_Rational a, mrb_int e);
mrb_int sp_rational_round_i(sp_Rational a);              /* Rational#round (no digits) */
mrb_int sp_rational_idiv(sp_Rational a, sp_Rational b);  /* Rational#div (floor) */
mrb_int sp_rational_floor_i(sp_Rational a);              /* Rational#floor (no digits) */
mrb_int sp_rational_ceil_i(sp_Rational a);               /* Rational#ceil (no digits) */
sp_Rational sp_rational_round_prec(sp_Rational a, mrb_int nd);
sp_Rational sp_rational_truncate_prec(sp_Rational a, mrb_int nd);
sp_Rational sp_rational_mod(sp_Rational a, sp_Rational b);   /* Rational#% (floor) */
sp_Rational sp_rational_rem(sp_Rational a, sp_Rational b);   /* Rational#remainder */
sp_Rational sp_rational_floor_prec(sp_Rational a, mrb_int nd);
sp_Rational sp_rational_ceil_prec(sp_Rational a, mrb_int nd);
mrb_int sp_rational_cmp(sp_Rational a, sp_Rational b);
mrb_bool sp_rational_eq(sp_Rational a, sp_Rational b);
mrb_float sp_rational_to_f(sp_Rational a);
sp_Rational sp_float_to_rational(mrb_float f);          /* Float#to_r (exact) */
sp_Rational sp_float_rationalize(mrb_float f, mrb_float eps);  /* Float#rationalize(eps) */
sp_Rational sp_float_rationalize0(mrb_float f);         /* Float#rationalize (no arg) */
#endif /* SP_FORMAT_H */

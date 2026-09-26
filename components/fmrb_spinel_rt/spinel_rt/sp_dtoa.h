/* sp_dtoa.h -- locale-independent float <-> string conversion.
 *
 * mruby's fp_uscale.c (Russ Cox's Unrounded Scaling; see
 * https://research.swtch.com/fp), adapted for spinel. It is pure integer
 * arithmetic and never touches libc's locale-sensitive snprintf/strtod, so a
 * float always renders and parses with a `.` decimal point regardless of the
 * process locale -- the invariant Ruby guarantees. */
#ifndef SP_DTOA_H
#define SP_DTOA_H

#include "sp_types.h"

/* Shortest-round-trip and printf-style float formatting into `buf` (NUL
   terminated). fmt is one of 'e'/'f'/'g' (upper-case for E/upper output);
   prec is the precision, or -2 for shortest round-trip ('g' only). sign is a
   forced leading sign ('+'/' ') or '\0'. Returns the written length. */
int sp_format_float(sp_float f, char *buf, size_t buf_size, char fmt, int prec, char sign);

/* Parse a float prefix of `str` (leading space skipped). On success stores the
   value in *fp, sets *endp past the consumed text, and returns TRUE. */
sp_bool sp_read_float(const char *str, char **endp, double *fp);

/* Shortest-round-trip significant digits of a finite non-zero f: writes the
   NUL-terminated digits to `digs`, their count to *nd, and returns the base-10
   exponent of the leading digit. The caller applies its own fixed/scientific
   layout (Ruby's Float#to_s threshold is not %g's). */
int sp_float_shortest(double f, char *digs, int *nd);

#endif

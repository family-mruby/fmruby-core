/* sp_time.h -- Time value type + libc-backed accessors / formatters.
 *
 * The whole of spinel's Time implementation lives in lib/sp_time.c
 * (compiled into libspinel_rt.a), including the formatters, which now
 * return GC-heap strings directly (sp_time.c includes sp_alloc.h) — so
 * the generated TU calls them straight, with no buffer-copying
 * trampoline in sp_runtime.h.
 *
 * The value ops (constructors, accessors, shifts) use int64_t / double
 * directly, keeping that part decoupled from the runtime's typedefs.
 */
#ifndef SP_TIME_H
#define SP_TIME_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* is_utc is a 3-state zone kind: 0 = host-local zone, 1 = UTC, 2 = a fixed
   utc_off-seconds offset (Time.new's 7th argument). utc_off is meaningful
   only for kind 2; the positional (sp_Time){sec, ns, kind} initializers used
   throughout leave it zero. */
typedef struct { int64_t tv_sec; int32_t tv_nsec; int8_t is_utc; int32_t utc_off; } sp_Time;

/* Constructors */
sp_Time sp_time_now(void);
sp_Time sp_time_at_int(int64_t sec);
sp_Time sp_time_at_float(double epoch);
sp_Time sp_time_at_div(int64_t num, int64_t den);
sp_Time sp_time_parse(const char *s);
sp_Time sp_time_new(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s);
sp_Time sp_time_new_utc(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s);
sp_Time sp_time_new_off(int64_t y, int64_t mo, int64_t d, int64_t h, int64_t mi, int64_t s, int64_t off);
sp_Time sp_time_with_usec(sp_Time t, int64_t usec);
sp_Time sp_time_utc(sp_Time t);
sp_Time sp_time_localtime(sp_Time t);

/* Broken-down resolver: is_utc selects gmtime vs localtime, off is the
   UTC offset in seconds, zbuf is the timezone abbreviation (8 bytes). */
void sp_time_vtm(sp_Time t, struct tm *bd, int32_t *off, char *zbuf);

/* Field accessors */
int64_t sp_time_year(sp_Time t);
int64_t sp_time_mon(sp_Time t);
int64_t sp_time_mday(sp_Time t);
int64_t sp_time_hour(sp_Time t);
int64_t sp_time_min(sp_Time t);
int64_t sp_time_sec(sp_Time t);
int64_t sp_time_wday(sp_Time t);
int64_t sp_time_yday(sp_Time t);
int64_t sp_time_isdst(sp_Time t);
int64_t sp_time_utc_offset(sp_Time t);

/* Time + Numeric / Time - Numeric. secs may be fractional. */
sp_Time sp_time_add(sp_Time t, double secs);

/* Comparison + integer/float shifts + Time-Time difference (cold value ops). */
int sp_time_cmp(sp_Time a, sp_Time b);
int64_t sp_time_hash(sp_Time t);
sp_Time sp_time_add_f(sp_Time t, double secs);
sp_Time sp_time_add_i(sp_Time t, int64_t secs);
sp_Time sp_time_sub_i(sp_Time t, int64_t secs);
double sp_time_sub_t(sp_Time a, sp_Time b);

/* Formatters: return a GC-heap string (from sp_alloc.h), "" on failure. */
const char *sp_time_strftime(sp_Time t, const char *fmt);
const char *sp_time_iso8601(sp_Time t);
const char *sp_time_zone(sp_Time t);
const char *sp_time_inspect_v(sp_Time t);  /* renders fractional seconds */
const char *sp_time_to_s_v(sp_Time t);     /* whole seconds only (Time#to_s) */

#endif

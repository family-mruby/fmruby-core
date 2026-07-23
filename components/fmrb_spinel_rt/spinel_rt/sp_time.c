/* sp_time.c -- libc-backed Time implementations.
 *
 * Sibling to sp_bigint.c / sp_crypto.c. The libc value ops (construct,
 * accessors, shifts) carry no runtime dependency; the formatters
 * (strftime / iso8601 / zone / inspect) return GC-heap strings directly
 * via sp_alloc.h, so the generated TU no longer needs buffer-copying
 * trampolines for them.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sp_time.h"
#include "sp_alloc.h"   /* sp_str_dup_external / sp_str_empty for the GC formatters */

sp_Time sp_time_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (sp_Time){ ts.tv_sec, (int32_t)ts.tv_nsec, 0 };
}

sp_Time sp_time_at_int(int64_t sec) {
  return (sp_Time){ sec, 0, 0 };
}

/* Time.at(Rational): the exact num/den epoch, floored to the nanosecond
   (Time.at(-1r/3) is second -1, nanosecond 666666666). den is positive by
   sp_Rational's normalized-sign invariant. */
sp_Time sp_time_at_div(int64_t num, int64_t den) {
  if (den == 0) sp_raise_cls("ZeroDivisionError", "divided by 0");
  int64_t sec = num / den;
  int64_t rem = num % den;
  if (rem < 0) { sec -= 1; rem += den; }
#if defined(__SIZEOF_INT128__)
  int64_t ns = (int64_t)(((__int128)rem * 1000000000) / den);
#else
  /* 32-bit targets (i386, Xtensa/ESP32) lack __int128. Fall back to double:
     exact to the mantissa's ~53 bits, so the last-ULP nanosecond of
     Time.at(Rational) is a documented 32-bit degradation, not a hard failure. */
  int64_t ns = (int64_t)((double)rem * 1000000000.0 / (double)den);
#endif
  return (sp_Time){ sec, (int32_t)ns, 0 };
}

/* Exact Float -> (sec, nsec) shift. CRuby converts the Float through to_r,
   so the EXACT binary value of the double decides the nanosecond, floored
   (Time.at(100) - 1.3 has usec 699999, not 700000, because the double
   nearest -1.3 is -1.3000000000000000444...). Reproduce that with integer
   math: decompose the double into mantissa * 2^exp via frexp, widen the
   mantissa-nanoseconds product to 128 bits, and arithmetic-shift (which
   floors negatives) instead of multiplying in double precision. */
static void sp_time_shift_ns(double secs, int64_t base_sec, int32_t base_ns,
                             int64_t *out_sec, int32_t *out_ns) {
  /* CRuby routes the Float through to_r, so a non-finite value raises
     FloatDomainError (Time.at(Float::INFINITY), t + Float::NAN, ...) rather
     than reaching frexp -- where (int64_t)(NaN/Inf * 2^53) would be UB. */
  if (!isfinite(secs))
    sp_raise_cls("FloatDomainError", isnan(secs) ? "NaN" : (secs < 0 ? "-Infinity" : "Infinity"));
  int e;
  double m = frexp(secs, &e);
  int64_t mi = (int64_t)(m * 9007199254740992.0); /* m * 2^53, exact */
  e -= 53;
#if defined(__SIZEOF_INT128__)
  __int128 ns = (__int128)mi * 1000000000;
  if (e > 0) ns = (e > 34) ? (ns < 0 ? INT64_MIN : INT64_MAX) : ns << e;
  else if (e < 0) ns = (-e > 126) ? (ns < 0 ? -1 : 0) : ns >> -e;
  __int128 total = ((__int128)base_sec * 1000000000 + base_ns) + ns;
  int64_t sec = (int64_t)(total / 1000000000);
  int64_t rem = (int64_t)(total % 1000000000);
#else
  /* 32-bit fallback (no __int128): shift the nanosecond product in double.
     Exact for typical epochs; large-magnitude or last-ULP-exact cases degrade
     to double precision -- a documented 32-bit limitation (see sp_time_at_div). */
  double nsd = (double)mi * 1000000000.0;
  if (e > 0) nsd = ldexp(nsd, e);
  else if (e < 0) nsd = ldexp(nsd, e);
  double totald = ((double)base_sec * 1000000000.0 + (double)base_ns) + nsd;
  int64_t sec = (int64_t)(totald / 1000000000.0);
  int64_t rem = (int64_t)(totald - (double)sec * 1000000000.0);
#endif
  if (rem < 0) { sec -= 1; rem += 1000000000; }
  *out_sec = sec;
  *out_ns = (int32_t)rem;
}

/* POSIX convention: keep tv_nsec in [0, 1e9). For negative epoch with
   a non-integer fractional part, decrement tv_sec and roll the fraction
   into the positive nsec range — so Time.at(-0.5).to_i returns -1, not 0. */
sp_Time sp_time_at_float(double epoch) {
  sp_Time r = { 0, 0, 0 };
  sp_time_shift_ns(epoch, 0, 0, &r.tv_sec, &r.tv_nsec);
  return r;
}

/* Time.new(y[,mo[,d[,h[,mi[,s]]]]]) — local construction. mktime
   interprets the broken-down value in the host local zone and resolves
   DST itself (tm_isdst=-1). The fixed-offset 7-arg form is a separate
   issue. */
sp_Time sp_time_new(int64_t y, int64_t mo, int64_t d,
                    int64_t h, int64_t mi, int64_t s) {
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = (int)y - 1900;
  tm.tm_mon  = (int)mo - 1;
  tm.tm_mday = (int)d;
  tm.tm_hour = (int)h;
  tm.tm_min  = (int)mi;
  tm.tm_sec  = (int)s;
  tm.tm_isdst = -1;
  time_t e = mktime(&tm);
  return (sp_Time){ (int64_t)e, 0, 0 };
}

/* Civil (y, mo, d, h, mi, s) -> UTC epoch seconds. Avoid timegm
   (not portable; absent on MSVCRT). Compute the epoch via Howard
   Hinnant's days_from_civil + a manual hour/minute/second add. */
static int64_t sp_time_civil_epoch(int64_t y, int64_t mo, int64_t d,
                                   int64_t h, int64_t mi, int64_t s) {
  int64_t yy = y - (mo <= 2 ? 1 : 0);
  int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
  int64_t yoe = yy - era * 400;
  int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = era * 146097 + doe - 719468;
  return days * 86400 + h * 3600 + mi * 60 + s;
}

/* Time.utc(y, m, d, h, mi, s) — UTC construction. */
sp_Time sp_time_new_utc(int64_t y, int64_t mo, int64_t d,
                        int64_t h, int64_t mi, int64_t s) {
  return (sp_Time){ sp_time_civil_epoch(y, mo, d, h, mi, s), 0, 1 };
}

/* Time.new(y, mo, d, h, mi, s, utc_offset) — the civil value is read in a
   fixed zone off seconds east of UTC, so the epoch is the UTC epoch of the
   same civil value minus that offset. CRuby bounds the offset to a day. */
sp_Time sp_time_new_off(int64_t y, int64_t mo, int64_t d,
                        int64_t h, int64_t mi, int64_t s, int64_t off) {
  if (off <= -86400 || off >= 86400)
    sp_raise_cls("ArgumentError", "utc_offset out of range");
  sp_Time t = { sp_time_civil_epoch(y, mo, d, h, mi, s) - off, 0, 2 };
  t.utc_off = (int32_t)off;
  return t;
}

/* Time.utc/local(y, mo, d, h, mi, s, usec) — the 7th positional argument is
   microseconds of second. */
sp_Time sp_time_with_usec(sp_Time t, int64_t usec) {
  if (usec < 0 || usec >= 1000000)
    sp_raise_cls("ArgumentError", "subsecx out of range");
  t.tv_nsec = (int32_t)(usec * 1000);
  return t;
}

/* Time.new(String): the fixed CRuby form "YYYY-MM-DD HH:MM:SS[.frac]" with
   an optional " +HH:MM" / " -HH:MM" / " UTC" zone suffix. A date without a
   time and any other shape raise CRuby's ArgumentError messages; anything
   the grammar does not cover must be loud, never a guessed instant. */
sp_Time sp_time_parse(const char *s) {
  const char *sp_sprintf(const char *fmt, ...);  /* generated TU */
  int y, mo, d, h, mi, sec, n = 0;
  if (sscanf(s, "%4d-%2d-%2d%n", &y, &mo, &d, &n) != 3 || n == 0)
    sp_raise_cls("ArgumentError", sp_sprintf("can't parse: \"%s\"", s));
  const char *p = s + n;
  if (*p == 0)
    sp_raise_cls("ArgumentError", "no time information");
  n = 0;
  if (sscanf(p, " %2d:%2d:%2d%n", &h, &mi, &sec, &n) != 3 || n == 0)
    sp_raise_cls("ArgumentError", sp_sprintf("can't parse: \"%s\"", s));
  p += n;
  int32_t nsec = 0;
  if (*p == '.') {
    p++;
    int digits = 0;
    int64_t frac = 0;
    while (*p >= '0' && *p <= '9' && digits < 9) { frac = frac * 10 + (*p - '0'); p++; digits++; }
    if (digits == 0)
      sp_raise_cls("ArgumentError", sp_sprintf("can't parse: \"%s\"", s));
    while (*p >= '0' && *p <= '9') p++;  /* sub-ns digits are beyond sp_Time */
    while (digits < 9) { frac *= 10; digits++; }
    nsec = (int32_t)frac;
  }
  while (*p == ' ') p++;
  if (*p == 0) {
    /* no zone: host-local, like the civil Time.new */
    sp_Time t = sp_time_new(y, mo, d, h, mi, sec);
    t.tv_nsec = nsec;
    return t;
  }
  if (strcmp(p, "UTC") == 0 || strcmp(p, "Z") == 0) {
    sp_Time t = sp_time_new_utc(y, mo, d, h, mi, sec);
    t.tv_nsec = nsec;
    return t;
  }
  int oh, om;
  n = 0;
  if ((*p == '+' || *p == '-') && sscanf(p + 1, "%2d:%2d%n", &oh, &om, &n) == 2 &&
      n > 0 && p[1 + n] == 0) {
    int64_t off = (int64_t)oh * 3600 + (int64_t)om * 60;
    if (*p == '-') off = -off;
    sp_Time t = sp_time_new_off(y, mo, d, h, mi, sec, off);
    t.tv_nsec = nsec;
    return t;
  }
  sp_raise_cls("ArgumentError", sp_sprintf("can't parse: \"%s\"", s));
}

sp_Time sp_time_utc(sp_Time t) {
  t.is_utc = 1;
  return t;
}

sp_Time sp_time_localtime(sp_Time t) {
  t.is_utc = 0;
  return t;
}

/* is_utc selects gmtime vs localtime, off is UTC offset in seconds,
   zbuf is the timezone abbreviation (8 bytes). mktime(gmtime(s))-s
   is the portable offset technique (MSVCRT's %z emits the timezone
   name, not ±HHMM). */
void sp_time_vtm(sp_Time t, struct tm *bd, int32_t *off, char *zbuf) {
  time_t s = (time_t)t.tv_sec;
  if (t.is_utc == 2) {
    /* fixed offset: the civil value is the UTC civil value shifted east */
    time_t sh = s + (time_t)t.utc_off;
    struct tm *g = gmtime(&sh);
    if (g) { *bd = *g; }
else { memset(bd, 0, sizeof(*bd)); }
    if (off) *off = t.utc_off;
    if (zbuf) zbuf[0] = 0;
  }
else if (t.is_utc) {
    struct tm *g = gmtime(&s);
    if (g) { *bd = *g; }
else { memset(bd, 0, sizeof(*bd)); }
    if (off) *off = 0;
    if (zbuf) { zbuf[0]='U'; zbuf[1]='T'; zbuf[2]='C'; zbuf[3]=0; }
  }
else {
    struct tm *l = localtime(&s);
    if (l) { *bd = *l; }
else { memset(bd, 0, sizeof(*bd)); }
    if (off) {
      struct tm gm = *gmtime(&s);
      gm.tm_isdst = -1;
      *off = (int32_t)(s - (time_t)mktime(&gm));
    }
    if (zbuf) {
      if (strftime(zbuf, 8, "%Z", bd) == 0) zbuf[0] = 0;
    }
  }
}

int64_t sp_time_year(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)(b.tm_year+1900);}
int64_t sp_time_mon(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)(b.tm_mon+1);}
int64_t sp_time_mday(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)b.tm_mday;}
int64_t sp_time_hour(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)b.tm_hour;}
int64_t sp_time_min(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)b.tm_min;}
int64_t sp_time_sec(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)b.tm_sec;}
int64_t sp_time_wday(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)b.tm_wday;}
int64_t sp_time_yday(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)(b.tm_yday+1);}
int64_t sp_time_isdst(sp_Time t){struct tm b;sp_time_vtm(t,&b,NULL,NULL);return (int64_t)(b.tm_isdst>0?1:0);}
int64_t sp_time_utc_offset(sp_Time t){int32_t o;struct tm b;sp_time_vtm(t,&b,&o,NULL);return (int64_t)o;}

/* Time + Numeric / Time - Numeric. secs may be fractional; the shift is
   exact in the double's binary value (see sp_time_shift_ns). The zone kind
   and offset are inherited from the receiver. */
sp_Time sp_time_add(sp_Time t, double secs) {
  sp_Time r = t;
  sp_time_shift_ns(secs, t.tv_sec, t.tv_nsec, &r.tv_sec, &r.tv_nsec);
  return r;
}

/* strftime returns 0 -- never overruns the buffer -- when the formatted
   result would exceed it, which we surface as "". The 4 KB buffer covers
   any realistic format (CRuby's built-ins are ~25 bytes; this leaves room
   for long literal text or wide fields). A pathological field width
   (`"%1000000000F"`, which CRuby rejects with ERANGE) does not fit and
   yields "" -- a graceful empty string rather than a crash. */
/* The UTC offset (seconds) of a Time, mirroring sp_time_iso8601's manual calc. */
static long sp_time_offset_sec(sp_Time t) {
  if (t.is_utc == 2) return t.utc_off;   /* fixed offset (Time.at in:) */
  if (t.is_utc) return 0;
  time_t s = (time_t)t.tv_sec;
  struct tm gm = *gmtime(&s);
  gm.tm_isdst = -1;
  return (long)(s - mktime(&gm));
}

/* Ruby-compatible strftime: C strftime handles the standard directives, but
   Ruby adds %L/%N (subsec), %s (epoch, which C's %s would take through a LOCAL
   mktime), %P (lowercase am/pm), the %:z/%::z colon offsets, and width/flag
   modifiers (%3S, %6N, %10Y). Walk the format, compute those directly, and
   pad; delegate a bare standard directive to strftime. (#2635, #2636) */
const char *sp_time_strftime(sp_Time t, const char *fmt) {
  time_t s = (time_t)t.tv_sec;
  struct tm tmv = t.is_utc ? *gmtime(&s) : *localtime(&s);
  static char out[8192];
  size_t oi = 0;
  for (const char *p = fmt; *p && oi < sizeof(out) - 128; p++) {
    if (*p != '%') { out[oi++] = *p; continue; }
    const char *tok = p++;
    int upcase = 0, downcase = 0, pad0 = 0, padsp = 0, nopad = 0, colon = 0;
    for (;; p++) {
      if (*p == '^') upcase = 1; else if (*p == '#') downcase = 1;
      else if (*p == '0') pad0 = 1; else if (*p == '_') padsp = 1;
      else if (*p == '-') nopad = 1; else break;
    }
    while (*p == ':') { colon++; p++; }
    int width = -1;
    if (*p >= '0' && *p <= '9') { width = 0; while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0'); }
    char d = *p;
    if (!d) { out[oi++] = '%'; break; }
    char val[128]; val[0] = 0;
    if (d == '%') { val[0] = '%'; val[1] = 0; }
    else if (d == 's') snprintf(val, sizeof val, "%lld", (long long)t.tv_sec);
    else if (d == 'L') snprintf(val, sizeof val, "%03d", (int)(t.tv_nsec / 1000000));
    else if (d == 'N') {
      int w = width > 0 ? width : 9;
      char nb[16]; snprintf(nb, sizeof nb, "%09ld", (long)t.tv_nsec);
      if (w <= 9) { memcpy(val, nb, (size_t)w); val[w] = 0; }
      else { strcpy(val, nb); for (int i = 9; i < w && i < 120; i++) val[i] = '0'; val[w < 120 ? w : 120] = 0; }
      width = -1;  /* width consumed by the subsecond precision */
    }
    else if (d == 'z') {
      /* bare %z computed from the receiver's own offset, never C strftime's
         tm_gmtoff -- that field is filled from the tm's construction path and
         varies by platform, and a fixed-offset time (is_utc == 2) has no tm
         representation at all (#2635) */
      if (!colon) {
        long off0 = sp_time_offset_sec(t);
        char sign0 = off0 < 0 ? '-' : '+'; long a0 = off0 < 0 ? -off0 : off0;
        snprintf(val, sizeof val, "%c%02d%02d", sign0, (int)(a0 / 3600), (int)((a0 / 60) % 60));
      }
      else {
        long off = sp_time_offset_sec(t);
        char sign = off < 0 ? '-' : '+'; long a = off < 0 ? -off : off;
        int oh = (int)(a / 3600), om = (int)((a / 60) % 60), os = (int)(a % 60);
        /* %:::z collapses to the minimal precision that loses nothing */
        if (colon >= 3) {
          if (os) snprintf(val, sizeof val, "%c%02d:%02d:%02d", sign, oh, om, os);
          else if (om) snprintf(val, sizeof val, "%c%02d:%02d", sign, oh, om);
          else snprintf(val, sizeof val, "%c%02d", sign, oh);
        }
        else if (colon == 2) snprintf(val, sizeof val, "%c%02d:%02d:%02d", sign, oh, om, os);
        else snprintf(val, sizeof val, "%c%02d:%02d", sign, oh, om);
      }
    }
    else if (d == 'P') { char b2[16]; strftime(b2, sizeof b2, "%p", &tmv); for (char *q = b2; *q; q++) *q = (char)tolower((unsigned char)*q); strcpy(val, b2); }
    else if (d == 'Z' && t.is_utc) strcpy(val, "UTC");  /* CRuby names a UTC time "UTC", not the C locale's "GMT" */
    else if (strchr("aAbBcCdDeFgGhHIjklmMnprRSTtuUvVwWxXyYzZ", d)) {
      /* a standard Ruby directive: format the bare `%X` (we redo width/case
         ourselves for portability) */
      char f2[3] = { '%', d, 0 };
      strftime(val, sizeof val, f2, &tmv);
    }
    else {
      /* not a Ruby Time#strftime directive (e.g. `%+`): emit the token
         verbatim -- CRuby does, and a platform's C strftime must not interpret
         it (macOS treats `%+` as date(1), glibc leaves it literal). */
      size_t tl = (size_t)(p - tok) + 1;
      if (tl < sizeof val) { memcpy(val, tok, tl); val[tl] = 0; }
      width = -1; upcase = downcase = 0;
    }
    if (upcase) for (char *q = val; *q; q++) *q = (char)toupper((unsigned char)*q);
    if (downcase) for (char *q = val; *q; q++) *q = (char)(isupper((unsigned char)*q) ? tolower((unsigned char)*q) : toupper((unsigned char)*q));
    size_t vl = strlen(val);
    if (width > 0 && !nopad && vl < (size_t)width) {
      char pc = padsp ? ' ' : '0';
      for (size_t k = vl; k < (size_t)width && oi < sizeof(out) - 2; k++) out[oi++] = pc;
    }
    (void)tok;
    for (size_t k = 0; k < vl && oi < sizeof(out) - 2; k++) out[oi++] = val[k];
  }
  out[oi] = 0;
  return sp_str_dup_external(out);
}

/* RFC 3339 / iso8601. Format date+time prefix with strftime, then
   compute the UTC offset manually via mktime(gmtime(s)) - s (MSVCRT
   %z renders the timezone name, so we do it ourselves). */
const char *sp_time_iso8601(sp_Time t) {
  char buf[64];
  size_t cap = sizeof(buf);
  time_t s = (time_t)t.tv_sec;
  if (t.is_utc) {
    struct tm *gt = gmtime(&s);
    if (gt == NULL) return sp_str_empty;
    size_t n = strftime(buf, cap, "%Y-%m-%dT%H:%M:%SZ", gt);
    if (n == 0) return sp_str_empty;
    return sp_str_dup_external(buf);
  }
  struct tm *lt = localtime(&s);
  if (lt == NULL) return sp_str_empty;
  size_t n = strftime(buf, cap, "%Y-%m-%dT%H:%M:%S", lt);
  if (n == 0) return sp_str_empty;
  if (n + 6 < cap) {
    struct tm gm = *gmtime(&s);
    gm.tm_isdst = -1;
    time_t gm_as_if_local = mktime(&gm);
    long offset_sec = (long)(s - gm_as_if_local);
    char sign = offset_sec >= 0 ? '+' : '-';
    long abs_off = offset_sec < 0 ? -offset_sec : offset_sec;
    int oh = (int)(abs_off / 3600);
    int om = (int)((abs_off / 60) % 60);
    buf[n++] = sign;
    buf[n++] = (char)('0' + (oh / 10));
    buf[n++] = (char)('0' + (oh % 10));
    buf[n++] = ':';
    buf[n++] = (char)('0' + (om / 10));
    buf[n++] = (char)('0' + (om % 10));
    buf[n] = 0;
  }
  return sp_str_dup_external(buf);
}

const char *sp_time_zone(sp_Time t) {
  char buf[8];
  struct tm b;
  sp_time_vtm(t, &b, NULL, buf);
  return sp_str_dup_external(buf);
}

/* Scalar Time inspect. CRuby form: local "YYYY-MM-DD HH:MM:SS +0900",
   UTC "YYYY-MM-DD HH:MM:SS UTC". The poly-box path keeps its own
   sp_Time_inspect; this value-taking variant is for the scalar
   p/puts/to_s codegen path. */
static const char *sp_time_fmt(sp_Time t, int frac) {
  char buf[64];
  size_t cap = sizeof(buf);
  struct tm b;
  int32_t off;
  sp_time_vtm(t, &b, &off, NULL);
  size_t n = strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &b);
  if (n == 0) {
    snprintf(buf, cap, "Time(%lld)", (long long)t.tv_sec);
    return sp_str_dup_external(buf);
  }
  if (frac && t.tv_nsec != 0 && n + 11 < cap) {
    /* fractional seconds, trailing zeros trimmed (".123456", ".5") */
    n += (size_t)snprintf(buf + n, cap - n, ".%09d", (int)t.tv_nsec);
    while (buf[n - 1] == '0') buf[--n] = 0;
  }
  if (n + 10 < cap) {
    if (t.is_utc == 1) {
      buf[n++]=' '; buf[n++]='U'; buf[n++]='T'; buf[n++]='C'; buf[n]=0;
    }
else {
      char sign = off >= 0 ? '+' : '-';
      long a = off < 0 ? -(long)off : (long)off;
      int oh = (int)(a / 3600);
      int om = (int)((a / 60) % 60);
      int os = (int)(a % 60);
      buf[n++]=' '; buf[n++]=sign;
      buf[n++]=(char)('0'+oh/10); buf[n++]=(char)('0'+oh%10);
      buf[n++]=(char)('0'+om/10); buf[n++]=(char)('0'+om%10);
      if (os) { /* CRuby renders a sub-minute offset as +HHMMSS */
        buf[n++]=(char)('0'+os/10); buf[n++]=(char)('0'+os%10);
      }
      buf[n]=0;
    }
  }
  return sp_str_dup_external(buf);
}

/* Time#inspect renders fractional seconds; Time#to_s does not. */
const char *sp_time_inspect_v(sp_Time t) { return sp_time_fmt(t, 1); }
const char *sp_time_to_s_v(sp_Time t)    { return sp_time_fmt(t, 0); }

/* ---- comparison + shifts (moved from sp_runtime.h; cold) ---- */
int sp_time_cmp(sp_Time a, sp_Time b) {
  if (a.tv_sec < b.tv_sec) return -1;
  if (a.tv_sec > b.tv_sec) return 1;
  if (a.tv_nsec < b.tv_nsec) return -1;
  if (a.tv_nsec > b.tv_nsec) return 1;
  return 0;
}
sp_Time sp_time_add_f(sp_Time t, double secs) {
  return sp_time_add(t, secs);
}
/* Value hash: equal (tv_sec, tv_nsec) pairs must hash equal regardless of
   zone kind, mirroring Time#== which compares the instant only. */
int64_t sp_time_hash(sp_Time t) {
  uint64_t h = (uint64_t)t.tv_sec * 1000000007ULL + (uint64_t)(uint32_t)t.tv_nsec;
  h ^= h >> 33;
  return (int64_t)(h & 0x3fffffffffffffffULL);
}
sp_Time sp_time_add_i(sp_Time t, int64_t secs) {
  sp_Time r = t;
  r.tv_sec = t.tv_sec + (time_t)secs;
  return r;
}
sp_Time sp_time_sub_i(sp_Time t, int64_t secs) {
  sp_Time r = t;
  r.tv_sec = t.tv_sec - (time_t)secs;
  return r;
}
double sp_time_sub_t(sp_Time a, sp_Time b) {
  return (double)(a.tv_sec - b.tv_sec) + ((double)(a.tv_nsec - b.tv_nsec) / 1e9);
}

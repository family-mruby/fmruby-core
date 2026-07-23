/* Cold, out-of-line helpers extracted from sp_runtime.h.
 *
 * Functions that are large and not on any hot path were `static` in
 * sp_runtime.h, so every translation unit that includes the header (every
 * generated program, plus each lib TU) compiled its own copy. Moving them here
 * -- a single TU that includes the full runtime header -- gives one linked copy
 * and shrinks per-TU compile work. sp_runtime.h keeps an `extern` declaration
 * so all callers still resolve. Only functions that call no program-generated
 * static (sp_sym_to_s / sp_sym_intern / ...) can live here, and each includes
 * only the low-level headers its dependencies need -- NOT sp_runtime.h, which
 * is a monolithic definition header meant to be compiled once (into the
 * generated program), so including it here would multiply-define the runtime. */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sp_alloc.h"   /* sp_str_alloc / sp_str_set_len / sp_raise_cls */
#include "sp_array.h"   /* sp_StrArray for Dir.glob */
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "sp_time.h"   /* sp_Time for File.mtime */
#include "sp_io.h"     /* sp_file_directory prototype */
#include "sp_str.h"
#include "sp_string.h"
#include "sp_system.h" /* sp_last_status for backtick */

/* Integer#% / Kernel#format "%b"/"%B": binary formatting with Ruby's flag,
   width, precision, and two's-complement-for-negative rules. */
int sp_fmt_binary(const char *spec, size_t sl, char conv, long long val,
                  char *out, size_t osz) {
  /* parse "%<flags><width>.<prec>b" out of spec[0..sl-1] (spec[sl-1] == conv) */
  int f_minus = 0, f_plus = 0, f_space = 0, f_hash = 0, f_zero = 0;
  size_t i = 1;
  for (; i < sl; i++) {
    if (spec[i] == '-') f_minus = 1;
    else if (spec[i] == '+') f_plus = 1;
    else if (spec[i] == ' ') f_space = 1;
    else if (spec[i] == '#') f_hash = 1;
    else if (spec[i] == '0') f_zero = 1;
    else break;
  }
  int width = 0;
  for (; i < sl && spec[i] >= '0' && spec[i] <= '9'; i++) width = (width * 10) + (spec[i] - '0');
  int prec = -1;
  if (i < sl && spec[i] == '.') {
    i++; prec = 0;
    for (; i < sl && spec[i] >= '0' && spec[i] <= '9'; i++) prec = (prec * 10) + (spec[i] - '0');
  }

  int neg = val < 0;
  /* Ruby shows the two's-complement ".." body only for a negative value with no
     sign flag; a + or space flag switches to signed magnitude ("-101"). */
  int twos = neg && !f_plus && !f_space;
  char digits[256]; int dn = 0;
  if (twos) {
    unsigned long long uv = (unsigned long long)val;
    int p = -1;  /* highest 0-bit position; -1 means val == -1 (all ones) */
    for (int bit = 62; bit >= 0; bit--) if (!((uv >> bit) & 1ULL)) { p = bit; break; }
    int ndig = p + 2; if (ndig < 1) ndig = 1;
    for (int bit = ndig - 1; bit >= 0; bit--) digits[dn++] = (char)('0' + (int)((uv >> bit) & 1ULL));
  } else {
    /* signed magnitude: |val| in binary. 0 has no significant digits, so it
       contributes a single '0' only when precision is not 0. */
    unsigned long long mag = neg ? (unsigned long long)(-(val + 1)) + 1 : (unsigned long long)val;
    if (mag == 0) { if (prec != 0) digits[dn++] = '0'; }
    else { char t[80]; int tn = 0; while (mag) { t[tn++] = (char)('0' + (int)(mag & 1ULL)); mag >>= 1; }
           while (tn) digits[dn++] = t[--tn]; }
  }
  /* precision: minimum digit count. The ".." body counts as 2 toward it and pads
     with the sign bit (1); signed magnitude pads with 0. */
  if (prec >= 0) {
    int target = twos ? (prec - 2) : prec;
    char padc = twos ? '1' : '0';
    int t2 = target - dn;
    if (t2 > 0) {
      /* clamp to the digits buffer (output is capped at osz anyway) */
      if (t2 + dn >= (int)sizeof(digits)) t2 = (int)sizeof(digits) - dn - 1;
      memmove(digits + t2, digits, (size_t)dn);  /* shift body right */
      memset(digits, padc, (size_t)t2);           /* leading padding */
      dn += t2;
    }
    f_zero = 0;  /* precision disables 0-flag for integer conversions */
  }
  /* assemble sign/prefix + body, then apply width padding */
  char body[300]; int bn = 0;
  char sign = twos ? 0 : (neg ? '-' : (f_plus ? '+' : (f_space ? ' ' : 0)));
  char prefix0 = 0, prefix1 = 0;
  if (f_hash && val != 0) { prefix0 = '0'; prefix1 = (conv == 'B') ? 'B' : 'b'; }
  if (sign) body[bn++] = sign;
  if (prefix0) { body[bn++] = prefix0; body[bn++] = prefix1; }
  if (twos) { body[bn++] = '.'; body[bn++] = '.'; }
  for (int k = 0; k < dn; k++) body[bn++] = digits[k];

  int o = 0;
  int pad = width - bn;
  if (pad > 0 && !f_minus && f_zero) {
    /* zero-pad: emit sign/prefix/".." first, then fill, then the rest. A two's-
       complement body fills with the sign bit (1); signed magnitude with 0. */
    int head = (sign ? 1 : 0) + (prefix0 ? 2 : 0) + (twos ? 2 : 0);
    char fillc = twos ? '1' : '0';
    for (int k = 0; k < head && o < (int)osz; k++) out[o++] = body[k];
    for (int k = 0; k < pad && o < (int)osz; k++) out[o++] = fillc;
    for (int k = head; k < bn && o < (int)osz; k++) out[o++] = body[k];
  } else {
    if (pad > 0 && !f_minus) for (int k = 0; k < pad && o < (int)osz; k++) out[o++] = ' ';
    for (int k = 0; k < bn && o < (int)osz; k++) out[o++] = body[k];
    if (pad > 0 && f_minus) for (int k = 0; k < pad && o < (int)osz; k++) out[o++] = ' ';
  }
  return o;
}

/* File.expand_path(path[, base]): absolute, `.`/`..`/`//`-normalized path.
   Depends only on sp_alloc.h + libc; no program-generated symbols. */
const char *sp_file_expand_path(const char *path, const char *base) {
  char raw[8192];
  char cwd[4096];
  const char *home = getenv("HOME");
  if (!home) home = "";
  if (!path) path = "";

  /* The `%.4000s` precision caps each component so the compiler can
     prove the combined output (<= 8001) fits in the 8192 buffer -- this
     silences -Wformat-truncation (an error under the test harness's
     -Werror). 4000 chars/component is well past PATH_MAX, so real paths
     never truncate. */
  if (path[0] == '~' && (path[1] == '\0' || path[1] == '/')) {
    snprintf(raw, sizeof(raw), "%.4000s%.4000s", home, path + 1);
  }
  else if (path[0] == '/') {
    snprintf(raw, sizeof(raw), "%.4000s", path);
  }
  else {
    char basebuf[8192];
    const char *b;
    if (base && base[0]) {
      if (base[0] == '~' && (base[1] == '\0' || base[1] == '/')) {
        snprintf(basebuf, sizeof(basebuf), "%.4000s%.4000s", home, base + 1);
        b = basebuf;
      }
      else if (base[0] == '/') {
        b = base;
      }
      else {
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
        snprintf(basebuf, sizeof(basebuf), "%.4000s/%.4000s", cwd, base);
        b = basebuf;
      }
    }
    else {
      if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
      b = cwd;
    }
    snprintf(raw, sizeof(raw), "%.4000s/%.4000s", b, path);
  }

  /* Normalize: walk segments, collapsing `.`/`..`/`//`. seg_start[k]
     records the output length to roll back to when a `..` pops the
     k-th kept segment. */
  size_t rawlen = strlen(raw);
  char *out = sp_str_alloc(rawlen + 1);
  size_t seg_start[1024];
  int nseg = 0;
  size_t olen = 0;
  out[olen++] = '/';
  const char *p = raw;
  while (*p) {
    if (*p == '/') { p++; continue; }
    const char *q = p;
    while (*q && *q != '/') q++;
    size_t slen = (size_t)(q - p);
    if (slen == 1 && p[0] == '.') {
      /* current dir -- skip */
    }
    else if (slen == 2 && p[0] == '.' && p[1] == '.') {
      if (nseg > 0) { nseg--; olen = seg_start[nseg]; }
    }
    else {
      size_t mark = olen;
      if (olen > 1) out[olen++] = '/';
      memcpy(out + olen, p, slen);
      olen += slen;
      if (nseg < 1024) seg_start[nseg++] = mark;
    }
    p = q;
  }
  out[olen] = 0;
  sp_str_set_len(out, olen);
  return out;
}

/* ---- String#to_c parse + Dir.glob (cold; moved from sp_runtime.h) ---- */

sp_Complex sp_str_to_c(const char *s) {
  double re = 0, im = 0;
  int parsed = 0;
  const char *fin = s;   /* first byte NOT consumed by the parse */
  if (s) {
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    char *end = NULL;
    double a = strtod(p, &end);
    if (end != p) {
      parsed = 1;
      /* rational-syntax component "n/d" */
      if (*end == '/') { const char *dp = end + 1; char *de = NULL; double d = strtod(dp, &de); if (de != dp) { a /= d; end = de; } }
      if (*end == 'i') { im = a; fin = end + 1; }
      else {
        re = a;
        const char *q = end;
        double b2 = strtod(q, &end);
        if (end != q) {
          if (*end == '/') { const char *dp = end + 1; char *de = NULL; double d = strtod(dp, &de); if (de != dp) { b2 /= d; end = de; } }
          if (*end == 'i') { im = b2; fin = end + 1; }
          else fin = q;   /* an imaginary number without the 'i' suffix ("1+2") is invalid */
        }
        else if ((*q == '+' || *q == '-') && q[1] == 'i') { im = (*q == '-') ? -1.0 : 1.0; fin = q + 2; }
        else fin = q;        /* "1+" and other incomplete forms leave the operator unconsumed */
      }
    }
    else if (*p == 'i') { im = 1; parsed = 1; fin = p + 1; }
    else if ((*p == '+' || *p == '-') && p[1] == 'i') { im = (*p == '-') ? -1.0 : 1.0; parsed = 1; fin = p + 2; }
  }
  /* the whole string must be consumed (only trailing whitespace allowed): an
     incomplete form like "1+" is invalid, not silently (1+0i) (#2617). */
  if (parsed) { while (*fin == ' ' || *fin == '\t') fin++; if (*fin != '\0') parsed = 0; }
  /* an unparseable string (or nil) is not a valid Complex value */
  if (!parsed) sp_raise_cls("ArgumentError", "invalid value for convert(): ");
  return (sp_Complex){ (mrb_float)re, (mrb_float)im };
}

/* FNM_DOTMATCH mode for the walk below: hidden entries (and ".") match a
   non-dot pattern; ".." never does, as CRuby's glob. Set by the _dot wrapper. */
static int sp_glob_dotmatch = 0;

int sp_fnmatch1(const char *pat, const char *str) {
  while (*pat) {
    if (*pat == '*') {
      pat++;
      if (!*pat) return 1;
      while (*str) { if (sp_fnmatch1(pat, str)) return 1; str++; }
      return sp_fnmatch1(pat, str);
    }
else if (*pat == '?') {
      if (!*str) return 0;
      pat++; str++;
    }
else {
      if (*pat != *str) return 0;
      pat++; str++;
    }
  }
  return *str == 0;
}

void sp_dir_glob_rec(const char *fsdir, const char *outprefix,
                            const char *tail, sp_StrArray *a) {
  DIR *d = opendir(fsdir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    const char *name = e->d_name;
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) continue;
    if (!sp_glob_dotmatch && name[0] == '.' && name[1] == 0) continue;
    char fspath[2048], outpath[2048];
    snprintf(fspath, sizeof fspath, "%s/%s", fsdir, name);
    snprintf(outpath, sizeof outpath, "%s%s", outprefix, name);
    if ((sp_glob_dotmatch || !(name[0] == '.' && tail[0] != '.')) && sp_fnmatch1(tail, name)) {
      char *copy = sp_str_alloc(strlen(outpath));
      strcpy(copy, outpath);
      sp_StrArray_push(a, copy);
    }
    if (name[0] != '.') {
      struct stat st;
      if (lstat(fspath, &st) == 0 && S_ISDIR(st.st_mode)) {
        char subprefix[2048];
        snprintf(subprefix, sizeof subprefix, "%s%s/", outprefix, name);
        sp_dir_glob_rec(fspath, subprefix, tail, a);
      }
    }
  }
  closedir(d);
}

sp_StrArray *sp_dir_glob(const char *pattern);
/* Dir.glob(pat, File::FNM_DOTMATCH) (#2828) */
sp_StrArray *sp_dir_glob_dot(const char *pattern) {
  sp_glob_dotmatch = 1;
  sp_StrArray *a = sp_dir_glob(pattern);
  sp_glob_dotmatch = 0;
  return a;
}
sp_StrArray *sp_dir_glob(const char *pattern) {
  /* the pattern is often a fresh interpolation temp, unrooted at the call
     site; the per-match sp_str_allocs below can collect it mid-walk */
  SP_GC_ROOT_STR(pattern);
  sp_StrArray *a = sp_StrArray_new();
  /* every matched entry sp_str_allocs inside the walk below, and enough of
     them trigger a collection mid-build -- root the result like
     sp_dir_entries_impl or it (and its pushed names) get swept under us */
  SP_GC_ROOT(a);
  if (!pattern) return a;
  /* Recursive double-star form: split at the double-star component. Everything
     before it is the output prefix (and, minus the trailing slash, the directory
     to walk); the component after it is the per-directory tail pattern. */
  const char *ss = strstr(pattern, "**");
  if (ss) {
    size_t plen = (size_t)(ss - pattern);
    if (plen >= 1024) return a;
    const char *after = ss + 2;
    if (*after == '/') after++;
    const char *tail = after;
    char outprefix[1024];
    memcpy(outprefix, pattern, plen);
    outprefix[plen] = 0;
    char fsdir[1024];
    if (plen == 0) {
      strcpy(fsdir, ".");
    }
    else {
      memcpy(fsdir, pattern, plen);
      fsdir[plen] = 0;
      if (fsdir[plen - 1] == '/') fsdir[plen - 1] = 0;
      if (fsdir[0] == 0) strcpy(fsdir, "/");
    }
    sp_dir_glob_rec(fsdir, outprefix, tail, a);
    sp_StrArray_sort_bang(a);
    return a;
  }
  const char *slash = strrchr(pattern, '/');
  char dirbuf[1024];
  const char *dirpath;
  const char *base_pat;
  if (slash) {
    size_t dl = (size_t)(slash - pattern);
    if (dl >= sizeof(dirbuf)) return a;
    memcpy(dirbuf, pattern, dl);
    dirbuf[dl] = 0;
    dirpath = (dl == 0) ? "/" : dirbuf;
    base_pat = slash + 1;
  }
else {
    dirpath = ".";
    base_pat = pattern;
  }
  DIR *d = opendir(dirpath);
  if (!d) return a;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    const char *name = e->d_name;
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) continue;
    if (!sp_glob_dotmatch && name[0] == '.' && base_pat[0] != '.') continue;
    if (sp_fnmatch1(base_pat, name)) {
      char full[2048];
      if (slash) snprintf(full, sizeof(full), "%s/%s", dirbuf, name);
      else snprintf(full, sizeof(full), "%s", name);
      char *copy = sp_str_alloc(strlen(full));
      strcpy(copy, full);
      sp_StrArray_push(a, copy);
    }
  }
  closedir(d);
  sp_StrArray_sort_bang(a);
  return a;
}

/* ---- File.read/size/mtime/join/readlines + Math.lgamma (cold) ---- */

const char *sp_file_join(const char **parts, int n) {
  /* CRuby boundary rule (#2785): exactly one separator joins adjacent
     components -- when the accumulated path already ends with '/', the next
     component's leading '/'s are dropped; otherwise one is inserted. */
  size_t total = 0;
  for (int i = 0; i < n; i++) { if (parts[i]) total += strlen(parts[i]); total++; }
  char *r = sp_str_alloc((mrb_int)total);
  size_t off = 0;
  for (int i = 0; i < n; i++) {
    const char *p = parts[i] ? parts[i] : "";
    if (i > 0) {
      if (off > 0 && r[off - 1] == '/') { while (*p == '/') p++; }
      else if (*p != '/') r[off++] = '/';
    }
    size_t l = strlen(p);
    memcpy(r + off, p, l); off += l;
  }
  r[off] = 0;
  sp_str_set_len(r, off);
  return r;
}

sp_StrArray *sp_file_readlines(const char *path) {
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  FILE *_fp = fopen(path ? path : "", "r");
  if (!_fp) return a;
  char _buf[4096];
  while (fgets(_buf, (int)sizeof(_buf), _fp)) {
    size_t _l = strlen(_buf);
    char *_r = sp_str_alloc_raw(_l + 1);
    memcpy(_r, _buf, _l + 1);
    sp_StrArray_push(a, _r);
  }
  fclose(_fp);
  return a;
}

sp_StrArray *sp_file_readlines_chomp(const char *path) {
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  FILE *_fp = fopen(path ? path : "", "r");
  if (!_fp) return a;
  char _buf[4096];
  while (fgets(_buf, (int)sizeof(_buf), _fp)) {
    size_t _l = strlen(_buf);
    if (_l > 0 && _buf[_l-1] == '\n') { _buf[--_l] = '\0'; }
    if (_l > 0 && _buf[_l-1] == '\r') { _buf[--_l] = '\0'; }
    char *_r = sp_str_alloc_raw(_l + 1);
    memcpy(_r, _buf, _l + 1);
    sp_StrArray_push(a, _r);
  }
  fclose(_fp);
  return a;
}

double sp_lgamma_pos(double x) {  /* x > 0 */
  if (x == 1.0 || x == 2.0) return 0.0;
  double corr = 0.0;
  while (x < 12.0) { corr -= log(x); x += 1.0; }
  double inv = 1.0 / x, inv2 = inv * inv;
  /* sum_{k>=1} B_2k / (2k(2k-1) x^(2k-1)) up to the 1/x^11 term */
  double series = (1.0/12.0) + (inv2 * (-(1.0/360.0) + (inv2 * ((1.0/1260.0)
                  + (inv2 * (-(1.0/1680.0) + (inv2 * (1.0/1188.0))))))));
  return corr + ((x - 0.5) * log(x)) - x + (0.5 * log(2.0 * M_PI)) + (series * inv);
}

sp_PolyArray *sp_math_lgamma(double x) {
  int sign = 1; double v;
  if (x > 0.0) {
    v = sp_lgamma_pos(x);
  }
  else {
    double s = sin(M_PI * x);
    if (s == 0.0) v = INFINITY;            /* pole at a non-positive integer */
    else { if (s < 0.0) sign = -1; v = log(M_PI / fabs(s)) - sp_lgamma_pos(1.0 - x); }
  }
  sp_PolyArray *r = sp_PolyArray_new(); SP_GC_ROOT(r);
  sp_PolyArray_push(r, sp_box_float(v));
  sp_PolyArray_push(r, sp_box_int(sign));
  return r;
}

const char *sp_file_read(const char *path) {
  if (sp_file_directory(path)) {
    sp_raise_cls("Errno::EISDIR", sp_sprintf("Is a directory @ io_fread - %s", path));
  }
  FILE *f = fopen(path, "r");
  if (!f) {
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" : errno == EACCES ? "Errno::EACCES" : "RuntimeError",
                 sp_sprintf("%s @ rb_sysopen - %s", strerror(errno), path));
    return &("\xff" "")[1];
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = sp_str_alloc(sz);
  size_t n = 0;
  if (sz > 0) {
    n = fread(buf, 1, sz, f);
  }
  buf[n] = 0;
  sp_str_set_len(buf, n);  /* a short read must not leave the size as length */
  fclose(f);
  return buf;
}

sp_Time sp_file_atime(const char *path) {
  if (!path) { sp_raise_cls("TypeError", "no implicit conversion of nil into String"); return (sp_Time){0, 0, 0}; }
  struct stat st;
  if (stat(path, &st) == -1) {
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" : "RuntimeError", sp_sprintf("%s @ File.atime - %s", strerror(errno), path));
    return (sp_Time){0, 0, 0};
  }
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
  return (sp_Time){(int64_t)st.st_atimespec.tv_sec, (int32_t)st.st_atimespec.tv_nsec, 0};
#else
  return (sp_Time){(int64_t)st.st_atim.tv_sec, (int32_t)st.st_atim.tv_nsec, 0};
#endif
}
sp_Time sp_file_ctime(const char *path) {
  if (!path) { sp_raise_cls("TypeError", "no implicit conversion of nil into String"); return (sp_Time){0, 0, 0}; }
  struct stat st;
  if (stat(path, &st) == -1) {
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" : "RuntimeError", sp_sprintf("%s @ File.ctime - %s", strerror(errno), path));
    return (sp_Time){0, 0, 0};
  }
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
  return (sp_Time){(int64_t)st.st_ctimespec.tv_sec, (int32_t)st.st_ctimespec.tv_nsec, 0};
#else
  return (sp_Time){(int64_t)st.st_ctim.tv_sec, (int32_t)st.st_ctim.tv_nsec, 0};
#endif
}
sp_Time sp_file_mtime(const char *path) {
  if (!path) {
    sp_raise_cls("TypeError", "no implicit conversion of nil into String");
    return (sp_Time){0, 0, 0};
  }
  struct stat st;
  if (stat(path, &st) == -1) {
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" : "RuntimeError", sp_sprintf("%s @ File.mtime - %s", strerror(errno), path));
    return (sp_Time){0, 0, 0};
  }
#if defined(__APPLE__)
  return (sp_Time){(int64_t)st.st_mtimespec.tv_sec, (int32_t)st.st_mtimespec.tv_nsec, 0};
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
  return (sp_Time){(int64_t)st.st_mtimespec.tv_sec, (int32_t)st.st_mtimespec.tv_nsec, 0};
#else
  /* Linux / others with st_mtim */
  return (sp_Time){(int64_t)st.st_mtim.tv_sec, (int32_t)st.st_mtim.tv_nsec, 0};
#endif
}

mrb_int sp_file_size(const char *path) {
  if (!path) {
    sp_raise_cls("TypeError", "no implicit conversion of nil into String");
    return 0;
  }
  struct stat st;
  if (stat(path, &st) == -1) {
    int err = errno;  /* capture once: strerror() may clobber errno */
    sp_raise_cls(err == ENOENT ? "Errno::ENOENT" : "RuntimeError", sp_sprintf("%s @ File.size - %s", strerror(err), path));
    return 0;
  }
  /* off_t (typically 64-bit) into mrb_int (intptr_t -> 32-bit on a 32-bit
     build): guard the narrowing, as spinel does for int arithmetic. */
  if ((off_t)(mrb_int)st.st_size != st.st_size) {
    sp_raise_cls("RangeError", "file size out of range for Integer");
    return 0;
  }
  return (mrb_int)st.st_size;
}

sp_IntArray *sp_file_binread_bytes(const char *path) {
  if (sp_file_directory(path)) {
    sp_raise_cls("Errno::EISDIR", sp_sprintf("Is a directory @ io_fread - %s", path));
  }
  FILE *f = fopen(path, "rb");
  sp_IntArray *a = sp_IntArray_new();
  if (!f) {
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" : errno == EACCES ? "Errno::EACCES" : "RuntimeError",
                 sp_sprintf("%s @ rb_sysopen - %s", strerror(errno), path));
    return a;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *buf = (unsigned char *)malloc(sz > 0 ? (size_t)sz : 1);
  if (buf && sz > 0) {
    /* Use fread's actual byte count, not the raw file size — a
       partial read otherwise pushes uninitialized memory. */
    size_t r = fread(buf, 1, (size_t)sz, f);
    for (size_t i = 0; i < r; i++) sp_IntArray_push(a, (mrb_int)buf[i]);
  }
  free(buf);
  fclose(f);
  return a;
}

/* ---- more cold helpers: string/File/Dir/poly-combinatorics/misc ---- */

const char *sp_str_splice_at(const char *s, mrb_int from, mrb_int n, const char *val, int range_form) {
  if (!s) s = "";
  if (!val) val = "";
  mrb_int len = (mrb_int)sp_str_length(s);
  if (n < 0) { sp_raise_cls("IndexError", sp_sprintf("negative length %lld", (long long)n)); return s; }
  mrb_int from0 = from;
  if (from < 0) from += len;
  if (from < 0 || from > len) {
    if (range_form) sp_raise_cls("RangeError", sp_sprintf("%lld out of range", (long long)from0));
    else sp_raise_cls("IndexError", sp_sprintf("index %lld out of string", (long long)from0));
    return s;
  }
  if (from + n > len) n = len - from;
  return sp_str_concat(sp_str_concat(sp_str_sub_range(s, 0, from), val),
                       sp_str_sub_range(s, from + n, len - from - n));
}

sp_FloatArray *sp_frange_step(sp_FloatRange r, mrb_float st) {
  sp_FloatArray *a = sp_FloatArray_new(); SP_GC_ROOT(a);
  if (st == 0) sp_raise_cls("ArgumentError", "step can't be 0");
  mrb_float span = r.last - r.first;
  if (span == span && st == st) {   /* not NaN */
    if ((span > 0 && st < 0) || (span < 0 && st > 0)) return a;   /* wrong direction */
  }
  mrb_int n = (mrb_int)(span / st + (r.excl ? -1e-9 : 1e-9));
  for (mrb_int i = 0; i <= n; i++) {
    mrb_float v = r.first + (mrb_float)i * st;
    if (st > 0) { if (r.excl ? v >= r.last : v > r.last + 1e-9) break; }
    else        { if (r.excl ? v <= r.last : v < r.last - 1e-9) break; }
    sp_FloatArray_push(a, v);
  }
  return a;
}

mrb_int sp_poly_cmp_int_arrays(sp_RbVal a, sp_RbVal b, mrb_bool *comparable) {
  if (a.tag != SP_TAG_OBJ || b.tag != SP_TAG_OBJ ||
      a.cls_id != SP_BUILTIN_INT_ARRAY || b.cls_id != SP_BUILTIN_INT_ARRAY) { *comparable = FALSE; return 0; }
  sp_IntArray *x = (sp_IntArray *)a.v.p, *y = (sp_IntArray *)b.v.p;
  if (!x || !y) { *comparable = FALSE; return 0; }
  mrb_int n = x->len < y->len ? x->len : y->len;
  for (mrb_int i = 0; i < n; i++) {
    mrb_int xe = x->data[x->start + i], ye = y->data[y->start + i];
    if (xe != ye) { *comparable = TRUE; return xe < ye ? -1 : 1; }
  }
  *comparable = TRUE;
  return (x->len > y->len) - (x->len < y->len);
}

mrb_int sp_int_round_half(mrb_int v, mrb_int nd, int mode) {
  if (nd >= 0) return v;
  mrb_int f = 1;
  for (mrb_int i = 0; i < -nd; i++) f *= 10;
  mrb_int q = v / f, rem = v % f;
  mrb_int arem = rem < 0 ? -rem : rem;
  mrb_int half = f / 2;
  int up;
  if (arem > half) up = 1;
  else if (arem < half) up = 0;
  else up = mode == 1 ? 1 : mode == 2 ? 0 : (q % 2 != 0);  /* :even ties to even */
  if (up) q += (v < 0 ? -1 : 1);
  return q * f;
}

sp_RbVal sp_poly_replace(sp_RbVal recv, sp_RbVal src) {
  if (recv.tag != SP_TAG_OBJ || src.tag != SP_TAG_OBJ) return recv;
  if (recv.cls_id == SP_BUILTIN_INT_ARRAY && src.cls_id == SP_BUILTIN_INT_ARRAY)
    sp_IntArray_replace((sp_IntArray *)recv.v.p, (sp_IntArray *)src.v.p);
  else if (recv.cls_id == SP_BUILTIN_FLT_ARRAY && src.cls_id == SP_BUILTIN_FLT_ARRAY)
    sp_FloatArray_replace((sp_FloatArray *)recv.v.p, (sp_FloatArray *)src.v.p);
  else if (recv.cls_id == SP_BUILTIN_STR_ARRAY && src.cls_id == SP_BUILTIN_STR_ARRAY)
    sp_StrArray_replace((sp_StrArray *)recv.v.p, (sp_StrArray *)src.v.p);
  else if (recv.cls_id == SP_BUILTIN_POLY_ARRAY) {
    sp_PolyArray *d = (sp_PolyArray *)recv.v.p;
    d->len = 0;
    switch (src.cls_id) {
      case SP_BUILTIN_INT_ARRAY: { sp_IntArray *s = (sp_IntArray *)src.v.p; for (mrb_int i = 0; i < s->len; i++) sp_PolyArray_push(d, sp_box_int(s->data[s->start + i])); break; }
      case SP_BUILTIN_FLT_ARRAY: { sp_FloatArray *s = (sp_FloatArray *)src.v.p; for (mrb_int i = 0; i < s->len; i++) sp_PolyArray_push(d, sp_box_float(s->data[i])); break; }
      case SP_BUILTIN_STR_ARRAY: { sp_StrArray *s = (sp_StrArray *)src.v.p; for (mrb_int i = 0; i < s->len; i++) sp_PolyArray_push(d, sp_box_str(s->data[i])); break; }
      case SP_BUILTIN_POLY_ARRAY: { sp_PolyArray *s = (sp_PolyArray *)src.v.p; for (mrb_int i = 0; i < s->len; i++) sp_PolyArray_push(d, s->data[i]); break; }
      default: break;
    }
  }
  return recv;
}

void sp_poly_combination_recur(sp_PolyArray *src, mrb_int start, mrb_int k, sp_PolyArray *acc, sp_PolyArray *out) {
  if (k == 0) {
    sp_PolyArray *cp = sp_PolyArray_new(); SP_GC_ROOT(cp);
    for (mrb_int i = 0; i < acc->len; i++) sp_PolyArray_push(cp, acc->data[i]);
    sp_PolyArray_push(out, sp_box_poly_array(cp));
    return;
  }
  for (mrb_int i = start; i <= src->len - k; i++) {
    sp_PolyArray_push(acc, src->data[i]);
    sp_poly_combination_recur(src, i + 1, k - 1, acc, out);
    acc->len--;
  }
}

void sp_poly_repeated_combination_recur(sp_PolyArray *src, mrb_int start, mrb_int k, sp_PolyArray *acc, sp_PolyArray *out) {
  if (k == 0) {
    sp_PolyArray *cp = sp_PolyArray_new(); SP_GC_ROOT(cp);
    for (mrb_int i = 0; i < acc->len; i++) sp_PolyArray_push(cp, acc->data[i]);
    sp_PolyArray_push(out, sp_box_poly_array(cp));
    return;
  }
  for (mrb_int i = start; i < src->len; i++) {
    sp_PolyArray_push(acc, src->data[i]);
    sp_poly_repeated_combination_recur(src, i, k - 1, acc, out);
    acc->len--;
  }
}

void sp_poly_permutation_recur(sp_PolyArray *src, mrb_int k, sp_IntArray *used, sp_PolyArray *acc, sp_PolyArray *out) {
  if (k == 0) {
    sp_PolyArray *cp = sp_PolyArray_new(); SP_GC_ROOT(cp);
    for (mrb_int i = 0; i < acc->len; i++) sp_PolyArray_push(cp, acc->data[i]);
    sp_PolyArray_push(out, sp_box_poly_array(cp));
    return;
  }
  for (mrb_int i = 0; i < src->len; i++) {
    if (used->data[used->start + i]) continue;
    used->data[used->start + i] = 1;
    sp_PolyArray_push(acc, src->data[i]);
    sp_poly_permutation_recur(src, k - 1, used, acc, out);
    acc->len--;
    used->data[used->start + i] = 0;
  }
}

void sp_poly_repeated_permutation_recur(sp_PolyArray *src, mrb_int k, sp_PolyArray *acc, sp_PolyArray *out) {
  if (k == 0) {
    sp_PolyArray *cp = sp_PolyArray_new(); SP_GC_ROOT(cp);
    for (mrb_int i = 0; i < acc->len; i++) sp_PolyArray_push(cp, acc->data[i]);
    sp_PolyArray_push(out, sp_box_poly_array(cp));
    return;
  }
  for (mrb_int i = 0; i < src->len; i++) {
    sp_PolyArray_push(acc, src->data[i]);
    sp_poly_repeated_permutation_recur(src, k - 1, acc, out);
    acc->len--;
  }
}

int sp_json_kind(sp_RbVal v) {
  if (v.tag != SP_TAG_OBJ) return 0;
  switch (v.cls_id) {
    case SP_BUILTIN_INT_ARRAY: case SP_BUILTIN_FLT_ARRAY:
    case SP_BUILTIN_STR_ARRAY: case SP_BUILTIN_SYM_ARRAY:
    case SP_BUILTIN_POLY_ARRAY: return 1;
    case SP_BUILTIN_STR_INT_HASH: case SP_BUILTIN_STR_STR_HASH:
    case SP_BUILTIN_INT_STR_HASH: case SP_BUILTIN_STR_POLY_HASH:
    case SP_BUILTIN_SYM_POLY_HASH: case SP_BUILTIN_POLY_POLY_HASH:
    case SP_BUILTIN_INT_INT_HASH:
      return 2;
    default: return 0;
  }
}

mrb_bool sp_poly_cbi_p(sp_RbVal v) {
  if (v.tag == SP_TAG_OBJ) {
    switch (v.cls_id) {
    case SP_BUILTIN_STR_INT_HASH: case SP_BUILTIN_STR_STR_HASH:
    case SP_BUILTIN_INT_STR_HASH: case SP_BUILTIN_STR_POLY_HASH:
    case SP_BUILTIN_SYM_POLY_HASH: case SP_BUILTIN_POLY_POLY_HASH:
    case SP_BUILTIN_INT_INT_HASH:
      return FALSE;
    default: break;
    }
  }
  sp_raise_cls("NoMethodError", "undefined method 'compare_by_identity?' for poly");
  return FALSE;
}

void sp_file_write(const char *path, const char *data) {
  if (sp_file_directory(path)) {
    sp_raise_cls("Errno::EISDIR", sp_sprintf("Is a directory @ rb_sysopen - %s", path));
  }
  FILE *f = fopen(path, "wb");
  if (!f) {
    sp_raise_cls(errno == ENOENT ? "Errno::ENOENT" : errno == EACCES ? "Errno::EACCES" : "RuntimeError",
                 sp_sprintf("%s @ rb_sysopen - %s", strerror(errno), path));
    return;
  }
  fwrite(data, 1, sp_str_byte_len(data), f);
  fclose(f);
}

const char *sp_backtick(const char *cmd) {
  FILE *p = popen(cmd, "r");
  if (!p) { sp_last_status = -1; return sp_str_empty; }
  char *buf = sp_str_alloc_raw(4096);
  size_t n = fread(buf, 1, 4095, p);
  buf[n] = 0;
  int st = pclose(p);
  /* Mirror sp_system_args' $? layout: POSIX pclose returns a wait-status,
     MSVCRT _pclose returns the plain exit code (shift to match). */
  sp_last_status = st;
  return buf;
}

const char *sp_file_basename(const char *path) {
  /* CRuby: trailing separators are ignored ("/a/b/" -> "b"); an all-separator
     path is "/" (#2784). */
  size_t end = strlen(path);
  while (end > 0 && path[end - 1] == '/') end--;
  if (end == 0) {
    if (path[0] == '/') { char *r = sp_str_alloc(1); r[0] = '/'; r[1] = 0; return r; }
    return sp_str_alloc(0);
  }
  size_t start = end;
  while (start > 0 && path[start - 1] != '/') start--;
  /* sp_gc_mark looks at byte[-1] to distinguish heap strings (`\xfe`)
     from literals (`\xff`). A mid-path pointer has whatever byte came
     before it, so return a fresh sp_str_alloc'd copy with the right marker. */
  size_t n = end - start;
  char *buf = sp_str_alloc((mrb_int)n);
  memcpy(buf, path + start, n);
  buf[n] = 0;
  return buf;
}
/* File.basename(path, suffix): ".*" strips the (non-leading) last extension,
   any other suffix strips a literal tail match (#2774). */
const char *sp_file_basename2(const char *path, const char *suffix) {
  const char *base = sp_file_basename(path);
  size_t n = strlen(base);
  if (suffix && strcmp(suffix, ".*") == 0) {
    const char *dot = strrchr(base, '.');
    if (dot && dot != base) n = (size_t)(dot - base);
  }
  else if (suffix && suffix[0]) {
    size_t sl = strlen(suffix);
    if (n > sl && strcmp(base + n - sl, suffix) == 0) n -= sl;
  }
  if (n == strlen(base)) return base;
  char *r = sp_str_alloc((mrb_int)n);
  memcpy(r, base, n); r[n] = 0;
  return r;
}

const char *sp_file_extname(const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  const char *dot = strrchr(base, '.');
  /* CRuby: leading-dot files (".bashrc") return "". Trailing-dot
     paths ("foo.") keep the dot since Ruby 2.7. */
  if (!dot || dot == base) return sp_str_empty;
  size_t n = strlen(dot);
  char *buf = sp_str_alloc(n);
  memcpy(buf, dot, n + 1);
  return buf;
}

sp_StrArray *sp_dir_entries_impl(const char *path, int children) {
  SP_GC_ROOT_STR(path);
  if (!path) sp_raise_cls("TypeError", "no implicit conversion of nil into String");
  DIR *d = opendir(path);
  if (!d) sp_raise_cls("Errno::ENOENT", sp_sprintf("No such file or directory @ dir_initialize - %s", path));
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    const char *name = e->d_name;
    if (children && name[0] == '.' &&
        (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
    char *copy = sp_str_alloc(strlen(name));
    strcpy(copy, name);
    sp_StrArray_push(a, copy);
  }
  closedir(d);
  sp_StrArray_sort_bang(a);
  return a;
}

sp_PolyArray *sp_poly_array_transpose(sp_PolyArray *rows) {
  SP_GC_SAVE();
  SP_GC_ROOT(rows);
  if (!rows || rows->len == 0) return sp_PolyArray_new();
  mrb_int nrows = rows->len;
  /* Determine column count and element kind from first non-empty row. */
  mrb_int ncols = 0;
  int16_t kind = 0; /* 0=unknown, SP_BUILTIN_INT_ARRAY, SP_BUILTIN_FLT_ARRAY, SP_BUILTIN_STR_ARRAY */
  for (mrb_int r = 0; r < nrows; r++) {
    sp_RbVal rv = rows->data[r];
    if (rv.tag != SP_TAG_OBJ) continue;
    mrb_int rlen = 0;
    if (rv.cls_id == SP_BUILTIN_INT_ARRAY)  { rlen = ((sp_IntArray *)rv.v.p)->len; if(!kind) kind = SP_BUILTIN_INT_ARRAY; }
    else if (rv.cls_id == SP_BUILTIN_FLT_ARRAY) { rlen = ((sp_FloatArray *)rv.v.p)->len; if(!kind) kind = SP_BUILTIN_FLT_ARRAY; }
    else if (rv.cls_id == SP_BUILTIN_STR_ARRAY) { rlen = ((sp_StrArray *)rv.v.p)->len; if(!kind) kind = SP_BUILTIN_STR_ARRAY; }
    if (rlen > ncols) ncols = rlen;
  }
  sp_PolyArray *result = sp_PolyArray_new();
  SP_GC_ROOT(result);
  for (mrb_int c = 0; c < ncols; c++) {
    sp_RbVal cv = sp_box_nil();
    if (kind == SP_BUILTIN_INT_ARRAY) {
      sp_IntArray *col = sp_IntArray_new();
      SP_GC_ROOT(col);
      for (mrb_int r = 0; r < nrows; r++) {
        sp_RbVal rv = rows->data[r];
        mrb_int val = SP_INT_NIL;
        if (rv.tag == SP_TAG_OBJ && rv.cls_id == SP_BUILTIN_INT_ARRAY) {
          sp_IntArray *row = (sp_IntArray *)rv.v.p;
          if (c < row->len) val = row->data[c];
        }
        sp_IntArray_push(col, val);
      }
      cv.tag = SP_TAG_OBJ; cv.cls_id = SP_BUILTIN_INT_ARRAY; cv.v.p = col;
    }
else if (kind == SP_BUILTIN_FLT_ARRAY) {
      sp_FloatArray *col = sp_FloatArray_new();
      SP_GC_ROOT(col);
      for (mrb_int r = 0; r < nrows; r++) {
        sp_RbVal rv = rows->data[r];
        mrb_float val = 0.0;
        if (rv.tag == SP_TAG_OBJ && rv.cls_id == SP_BUILTIN_FLT_ARRAY) {
          sp_FloatArray *row = (sp_FloatArray *)rv.v.p;
          if (c < row->len) val = row->data[c];
        }
        sp_FloatArray_push(col, val);
      }
      cv.tag = SP_TAG_OBJ; cv.cls_id = SP_BUILTIN_FLT_ARRAY; cv.v.p = col;
    }
else if (kind == SP_BUILTIN_STR_ARRAY) {
      sp_StrArray *col = sp_StrArray_new();
      SP_GC_ROOT(col);
      for (mrb_int r = 0; r < nrows; r++) {
        sp_RbVal rv = rows->data[r];
        const char *val = sp_str_empty;
        if (rv.tag == SP_TAG_OBJ && rv.cls_id == SP_BUILTIN_STR_ARRAY) {
          sp_StrArray *row = (sp_StrArray *)rv.v.p;
          if (c < row->len && row->data[c]) val = row->data[c];
        }
        sp_StrArray_push(col, val);
      }
      cv.tag = SP_TAG_OBJ; cv.cls_id = SP_BUILTIN_STR_ARRAY; cv.v.p = col;
    }
    sp_PolyArray_push(result, cv);
  }
  return result;
}

sp_PolyArray *sp_str_chars_poly(const char *s) {
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  if (!s) sp_nil_recv("chars");
  for (const char *p = s; *p; ) {
    int n = sp_utf8_advance(p);
    char *c = sp_str_alloc(n); memcpy(c, p, n); c[n] = 0;
    sp_PolyArray_push(a, sp_box_str(c));
    p += n;
  }
  return a;
}

/* ---- Native backtrace formatting (spinel --debug) ----
   Moved from sp_runtime.h. The capture itself (backtrace() at raise time)
   stays in the header next to sp_raise_cls; only the cold symbol->Ruby-frame
   formatting lives here. The two flag globals are defined here so the
   debug-build main() (generated TU) and the header callers share one copy. */
#include <execinfo.h>
int sp_bt_enabled = 0;          /* set to 1 by debug-build main() */
const char *sp_bt_srcfile = ""; /* toplevel .rb path, set by debug main() */
static int sp_bt_is_runtime(const char *n) {
  static const char *pfx[] = {
    "int_", "str_", "float_", "sym_", "gc_", "bigint", "sprintf", "raise",
    "exc_", "range", "utf8", "oom", "bt_", "backtrace", "caller", "StrArray",
    "IntArray", "FloatArray", "PtrArray", "PolyArray", "Str", "Int", "Float",
    "Hash", "Range", "Complex", "Rational", "Sym", "alloc", "free", "to_s",
    "dup", "new", "pack", "unpack", "regex", "re_",
    /* arithmetic/runtime helpers that can raise and sit between the raise
       and the user frame (ZeroDivisionError via sp_idiv/sp_imod, etc.) */
    "idiv", "imod", "gcd", "fdiv", "ipow", "iclamp", "div_", "mod_", 0
  };
  for (int i = 0; pfx[i]; i++) {
    size_t l = strlen(pfx[i]);
    if (strncmp(n, pfx[i], l) == 0) return 1;
  }
  return 0;
}

/* Extract the symbol token from a backtrace_symbols line. Two formats:
     - glibc/Linux: "<module>(<symbol>+0x<off>) [0x<addr>]". The symbol is
       empty for unresolved frames (static or stripped fns) -> skip.
     - macOS:       "<idx> <image> <addr> <symbol> + <off>".
   Returns NULL if it isn't a keepable user frame. Detect Linux by the '('
   that delimits the symbol (the macOS format has none). */
static const char *sp_bt_symbol(const char *line) {
  char sym[256];
  const char *lp = strchr(line, '(');
  if (lp) {                                       /* glibc/Linux paren form */
    const char *p = lp + 1;
    const char *end = p;
    while (*end && *end != '+' && *end != ')') end++;
    size_t len = (size_t)(end - p);
    if (len == 0 || len > 250) return 0;          /* unresolved (static/stripped) */
    memcpy(sym, p, len); sym[len] = 0;
  }
else {                                        /* macOS: "<idx> <image> <addr> <symbol> + <off>" */
    /* The symbol is the token just before the " + <off>" delimiter. Parse
       backward from the last " + " rather than forward from "0x" — an image
       path containing "0x" (e.g. /path/0x_proj/bin) would otherwise misparse. */
    const char *plus = 0, *q = line;
    while ((q = strstr(q, " + ")) != 0) { plus = q; q += 3; }
    if (!plus) return 0;
    const char *end = plus;
    const char *p = end;
    while (p > line && p[-1] != ' ') p--;          /* back up to the symbol's start */
    size_t len = (size_t)(end - p);
    if (len == 0 || len > 250) return 0;
    memcpy(sym, p, len); sym[len] = 0;
  }
  if (strcmp(sym, "main") == 0) return strdup("<main>");
  if (strncmp(sym, "sp_", 3) != 0) return 0;     /* skip non-Spinel frames */
  const char *name = sym + 3;
  if (sp_bt_is_runtime(name)) return 0;
  /* De-mangle sp_<Class>_<method> back to Ruby. A Spinel symbol is a path of
     CamelCase class segments (each from a `::`, joined by `_`) followed by the
     method; the method is the first segment that starts lowercase. A literal
     `cls` segment marks a singleton method:
       sp_Helper_cls_boom          -> Helper.boom
       sp_Tep_Url_parse_query      -> Tep::Url#parse_query
       sp_Tep_AuthOAuth2_cls_find  -> Tep::AuthOAuth2.find
       sp_toplevel                 -> toplevel   (top-level method, no class)
     (Method names stay sanitized — e.g. enabled? is enabled_p; reversing that
     needs the emitted name table, a separate refinement.) */
  const char *mstart = 0;   /* first lowercase-starting segment = the method */
  for (const char *p = name; *p; p++) {
    int seg_start = (p == name) || (p[-1] == '_');
    if (seg_start && *p >= 'a' && *p <= 'z') { mstart = p; break; }
  }
  if (!mstart) return strdup(name);            /* all-uppercase: leave as-is */
  if (mstart == name) {                        /* no class path: top-level */
    if (strncmp(name, "cls_", 4) == 0) return strdup(name + 4);  /* top-level singleton */
    return strdup(name);
  }
  char out[256]; size_t o = 0;
  const char *meth; char sep;
  if (strncmp(mstart, "cls_", 4) == 0) { meth = mstart + 4; sep = '.'; }  /* singleton */
  else                                 { meth = mstart;     sep = '#'; }  /* instance */
  for (const char *p = name; p < mstart - 1 && o + 2 < sizeof(out); p++) {
    if (*p == '_') { out[o++] = ':'; out[o++] = ':'; }   /* class-path `_` was a `::` */
    else out[o++] = *p;
  }
  if (o + 1 < sizeof(out)) out[o++] = sep;
  size_t ml = strlen(meth);
  if (o + ml < sizeof(out)) { memcpy(out + o, meth, ml); o += ml; }
  out[o] = 0;
  return strdup(out);
}

sp_StrArray *sp_bt_format(void **buf, int n) {
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  if (!sp_bt_enabled || n <= 0) return a;
  char **syms = backtrace_symbols(buf, n);
  if (!syms) return a;
  const char *src = (sp_bt_srcfile && sp_bt_srcfile[0]) ? sp_bt_srcfile : "(spinel)";
  for (int i = 0; i < n; i++) {
    char *name = (char *)sp_bt_symbol(syms[i]);  /* always strdup'd; free after use */
    if (!name) continue;
    sp_StrArray_push(a, sp_sprintf("%s:in `%s'", src, name));
    free(name);
  }
  free(syms);
  return a;
}

#include <fcntl.h>
#include <sys/file.h>
#include <fnmatch.h>
#include <pwd.h>
#include <sys/wait.h>

/* ---- File / Dir surface ops moved from sp_runtime.h ----
   Path-level libc wrappers (stat family, Dir handle ops, FileTest
   helpers): cold, and every dependency is lib-visible. Prototypes
   first: the bodies keep their original header order, but a few
   helpers were forward-referenced there. */
const char *sp_File_gets_sep(sp_File *f, const char *sep, mrb_int limit, mrb_bool chomp);
sp_StrArray *sp_File_readlines_sep(sp_File *f, const char *sep, mrb_bool chomp);
sp_StrArray *sp_file_readlines_sep(const char *path, const char *sep, mrb_bool chomp);
const char *sp_File_readline_sep(sp_File *f, const char *sep, mrb_int limit, mrb_bool chomp);
const char *sp_File_getc(sp_File *f);
const char *sp_File_readchar(sp_File *f);
mrb_int sp_File_getbyte(sp_File *f);
sp_RbVal sp_File_ungetc(sp_File *f, sp_RbVal v);
const char *sp_File_readpartial(sp_File *f, mrb_int n);
mrb_int sp_File_sysseek(sp_File *f, mrb_int off, mrb_int whence);
mrb_int sp_File_flock(sp_File *f, mrb_int op);
mrb_int sp_File_fsync(sp_File *f);
sp_RbVal sp_File_putc(sp_File *f, sp_RbVal v);
const char *sp_file_ftype(const char *path);
mrb_bool sp_file_writable(const char *path);
mrb_bool sp_file_executable(const char *path);
mrb_int sp_file_size_q(const char *path);
mrb_bool sp_file_pipe(const char *path);
mrb_bool sp_file_identical(const char *a, const char *b);
const char *sp_file_realpath(const char *path);
const char *sp_file_read_len(const char *path, mrb_int n);
mrb_int sp_file_chmod(mrb_int mode, const char *path);
mrb_int sp_file_truncate(const char *path, mrb_int n);
mrb_int sp_file_write_at(const char *path, const char *data, mrb_int off);
mrb_int sp_file_write_mode(const char *path, const char *data, const char *mode);
sp_File *sp_File_open_flags(const char *path, mrb_int fl);
void sp_file_stat_scan(void *p);
sp_File *sp_file_stat_handle(const char *path);
mrb_int sp_file_stat_mode(const char *path);
mrb_bool sp_file_fnmatch(const char *pat, const char *path);
sp_StrArray *sp_file_split(const char *path);
mrb_bool sp_file_zero(const char *path);
const char *sp_file_dirname(const char *path);
const char *sp_dir_pwd(void);
mrb_int sp_dir_mkdir(const char *path);
mrb_int sp_dir_rmdir(const char *path);
mrb_int sp_dir_chdir(const char *path);
const char *sp_dir_home(void);
void sp_Dir_fin(void *p);
void sp_Dir_scan(void *p);
sp_Dir *sp_Dir_new(const char *path);
const char *sp_Dir_read(sp_Dir *d);
const char *sp_Dir_path(sp_Dir *d);
sp_RbVal sp_Dir_close(sp_Dir *d);
sp_Dir *sp_Dir_rewind(sp_Dir *d);
mrb_int sp_Dir_tell(sp_Dir *d);
sp_StrArray *sp_dir_entries(const char *path);
mrb_bool sp_dir_empty(const char *path);
const char *sp_dir_home_user(const char *user);
sp_StrArray *sp_dir_children(const char *path);

const char *sp_File_gets_sep(sp_File *f, const char *sep, mrb_int limit, mrb_bool chomp) {
  if (!f || !f->fp) return NULL;
  size_t sl = sep ? strlen(sep) : 0;
  /* fast path: the default "\n" separator with no limit reads via fgets
     (the byte-wise loop below costs a call per character) */
  if (sl == 1 && sep[0] == '\n' && limit <= 0) {
    char buf[65536];
    if (!fgets(buf, (int)sizeof buf, f->fp)) return NULL;
    size_t n = strlen(buf);
    if (chomp && n && buf[n - 1] == '\n') n--;
    char *r = sp_str_alloc(n);
    memcpy(r, buf, n); r[n] = 0;
    sp_str_set_len(r, n);
    f->lineno++;
    return r;
  }
  size_t cap = 256, len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) return NULL;
  int ch;
  while ((ch = fgetc(f->fp)) != EOF) {
    if (len + 2 > cap) { cap *= 2; char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
    buf[len++] = (char)ch;
    if (sl && len >= sl && memcmp(buf + len - sl, sep, sl) == 0) break;
    if (limit > 0 && (mrb_int)len >= limit) break;
  }
  if (len == 0) { free(buf); return NULL; }
  if (chomp && sl && len >= sl && memcmp(buf + len - sl, sep, sl) == 0) len -= sl;
  char *r = sp_str_alloc(len);
  memcpy(r, buf, len); r[len] = 0;
  sp_str_set_len(r, len);
  free(buf);
  f->lineno++;
  return r;
}
sp_StrArray *sp_File_readlines_sep(sp_File *f, const char *sep, mrb_bool chomp) {
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  const char *l;
  while ((l = sp_File_gets_sep(f, sep, 0, chomp)) != NULL) sp_StrArray_push(a, l);
  return a;
}
sp_StrArray *sp_file_readlines_sep(const char *path, const char *sep, mrb_bool chomp) {
  sp_File *f = sp_File_open(path, "r");
  SP_GC_ROOT(f);
  sp_StrArray *a = sp_File_readlines_sep(f, sep, chomp);
  sp_File_close(f);
  return a;
}
const char *sp_File_readline_sep(sp_File *f, const char *sep, mrb_int limit, mrb_bool chomp) {
  const char *r = sp_File_gets_sep(f, sep, limit, chomp);
  if (!r) sp_raise_cls("EOFError", "end of file reached");
  return r;
}
const char *sp_File_getc(sp_File *f) {
  if (!f || !f->fp) return NULL;
  int ch = fgetc(f->fp);
  if (ch == EOF) return NULL;
  int extra = ((ch & 0xE0) == 0xC0) ? 1 : ((ch & 0xF0) == 0xE0) ? 2 : ((ch & 0xF8) == 0xF0) ? 3 : 0;
  char *r = sp_str_alloc((size_t)(1 + extra));
  size_t n = 0;
  r[n++] = (char)ch;
  for (int i = 0; i < extra; i++) {
    int c2 = fgetc(f->fp);
    if (c2 == EOF) break;
    r[n++] = (char)c2;
  }
  r[n] = 0;
  sp_str_set_len(r, n);
  return r;
}
const char *sp_File_readchar(sp_File *f) {
  const char *r = sp_File_getc(f);
  if (!r) sp_raise_cls("EOFError", "end of file reached");
  return r;
}
mrb_int sp_File_getbyte(sp_File *f) {
  if (!f || !f->fp) return SP_INT_NIL;
  int ch = fgetc(f->fp);
  return ch == EOF ? SP_INT_NIL : (mrb_int)ch;
}
sp_RbVal sp_File_ungetc(sp_File *f, sp_RbVal v) {
  if (f && f->fp) {
    if (v.tag == SP_TAG_STR && v.v.s && v.v.s[0]) {
      size_t n = sp_str_byte_len(v.v.s);
      for (size_t i = n; i > 0; i--) ungetc((unsigned char)v.v.s[i - 1], f->fp);
    }
    else if (v.tag == SP_TAG_INT) ungetc((int)v.v.i, f->fp);
  }
  return sp_box_nil();
}
const char *sp_File_readpartial(sp_File *f, mrb_int n) {
  if (!f || !f->fp || n < 0) sp_raise_cls("EOFError", "end of file reached");
  char *r = sp_str_alloc((size_t)n);
  size_t got = fread(r, 1, (size_t)n, f->fp);
  if (got == 0 && n > 0) sp_raise_cls("EOFError", "end of file reached");
  r[got] = 0;
  sp_str_set_len(r, got);
  return r;
}
mrb_int sp_File_sysseek(sp_File *f, mrb_int off, mrb_int whence) {
  if (!f || !f->fp) return 0;
  fseek(f->fp, (long)off, whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET);
  return (mrb_int)ftell(f->fp);
}
mrb_int sp_File_flock(sp_File *f, mrb_int op) {
  if (!f || !f->fp) return 0;
  return flock(fileno(f->fp), (int)op) == 0 ? 0 : 1;
}
mrb_int sp_File_fsync(sp_File *f) {
  if (!f || !f->fp) return 0;
  fflush(f->fp);
  fsync(fileno(f->fp));
  return 0;
}
sp_RbVal sp_File_putc(sp_File *f, sp_RbVal v) {
  if (f && f->fp) {
    if (v.tag == SP_TAG_INT) fputc((int)(v.v.i & 0xff), f->fp);
    else if (v.tag == SP_TAG_STR && v.v.s && v.v.s[0]) fputc(v.v.s[0], f->fp);
  }
  return v;
}
const char *sp_file_ftype(const char *path) {
  struct stat st;
  if (lstat(path ? path : "", &st) != 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_file_s_ftype - %s", path ? path : ""));
  if (S_ISREG(st.st_mode)) return (&("\xff" "file")[1]);
  if (S_ISDIR(st.st_mode)) return (&("\xff" "directory")[1]);
  if (S_ISLNK(st.st_mode)) return (&("\xff" "link")[1]);
  if (S_ISFIFO(st.st_mode)) return (&("\xff" "fifo")[1]);
  if (S_ISCHR(st.st_mode)) return (&("\xff" "characterSpecial")[1]);
  if (S_ISBLK(st.st_mode)) return (&("\xff" "blockSpecial")[1]);
#ifdef S_ISSOCK
  if (S_ISSOCK(st.st_mode)) return (&("\xff" "socket")[1]);
#endif
  return (&("\xff" "unknown")[1]);
}
mrb_bool sp_file_writable(const char *path) { return access(path ? path : "", W_OK) == 0; }
mrb_bool sp_file_executable(const char *path) { return access(path ? path : "", X_OK) == 0; }
mrb_int sp_file_size_q(const char *path) {   /* Integer size, or nil for missing/empty */
  struct stat st;
  if (stat(path ? path : "", &st) != 0 || st.st_size == 0) return SP_INT_NIL;
  return (mrb_int)st.st_size;
}
mrb_bool sp_file_pipe(const char *path) {
  struct stat st;
  return stat(path ? path : "", &st) == 0 && S_ISFIFO(st.st_mode);
}
mrb_bool sp_file_identical(const char *a, const char *b) {
  struct stat sa, sb;
  if (stat(a ? a : "", &sa) != 0 || stat(b ? b : "", &sb) != 0) return 0;
  return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}
const char *sp_file_realpath(const char *path) {
  char buf[4096];
  if (!realpath(path ? path : "", buf))
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ realpath_rec - %s", path ? path : ""));
  return sp_sprintf("%s", buf);
}
const char *sp_file_read_len(const char *path, mrb_int n) {
  FILE *fp = fopen(path ? path : "", "rb");
  if (!fp)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_sysopen - %s", path ? path : ""));
  if (n < 0) n = 0;
  char *r = sp_str_alloc(n);
  size_t got = fread(r, 1, (size_t)n, fp);
  fclose(fp);
  r[got] = 0;
  sp_str_set_len(r, got);
  return r;
}
mrb_int sp_file_chmod(mrb_int mode, const char *path) {
  if (chmod(path ? path : "", (mode_t)mode) != 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ apply2files - %s", path ? path : ""));
  return 1;
}
mrb_int sp_file_truncate(const char *path, mrb_int n) {
  if (truncate(path ? path : "", (off_t)n) != 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_file_s_truncate - %s", path ? path : ""));
  return 0;
}
mrb_int sp_file_write_at(const char *path, const char *data, mrb_int off) {
  int fd = open(path ? path : "", O_WRONLY | O_CREAT, 0666);
  if (fd < 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_sysopen - %s", path ? path : ""));
  size_t n = sp_str_byte_len(data ? data : "");
  ssize_t w = pwrite(fd, data ? data : "", n, (off_t)off);
  close(fd);
  return w < 0 ? 0 : (mrb_int)w;
}
mrb_int sp_file_write_mode(const char *path, const char *data, const char *mode) {
  FILE *fp = fopen(path ? path : "", mode && mode[0] ? mode : "w");
  if (!fp)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_sysopen - %s", path ? path : ""));
  size_t n = sp_str_byte_len(data ? data : "");
  fwrite(data ? data : "", 1, n, fp);
  fclose(fp);
  return (mrb_int)n;
}
sp_File *sp_File_open_flags(const char *path, mrb_int fl) {
  int fd = open(path ? path : "", (int)fl, 0666);
  if (fd < 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_sysopen - %s", path ? path : ""));
  int acc = (int)fl & O_ACCMODE;
  const char *m = (acc == O_RDONLY) ? "r"
                : (acc == O_WRONLY) ? (((int)fl & O_APPEND) ? "a" : "w")
                : (((int)fl & O_APPEND) ? "a+" : "r+");
  return sp_io_fdopen(fd, m);
}
void sp_file_stat_scan(void *p) {
  sp_File *f = (sp_File *)p;
  if (f->path) sp_mark_string(f->path);
  if (f->mode) sp_mark_string(f->mode);
}
sp_File *sp_file_stat_handle(const char *path) {
  struct stat st;
  if (stat(path ? path : "", &st) != 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_file_s_stat - %s", path ? path : ""));
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), NULL, sp_file_stat_scan);
  f->fp = NULL;
  f->path = sp_sprintf("%s", path ? path : "");
  f->mode = (&("\xff" "stat")[1]);
  f->lineno = 0;
  return f;
}
mrb_int sp_file_stat_mode(const char *path) {
  struct stat st;
  if (stat(path ? path : "", &st) != 0) return 0;
  return (mrb_int)st.st_mode;
}
mrb_bool sp_file_fnmatch(const char *pat, const char *path) {
  return fnmatch(pat ? pat : "", path ? path : "", FNM_PATHNAME | FNM_PERIOD) == 0;
}
sp_StrArray *sp_file_split(const char *path) {
  sp_StrArray *a = sp_StrArray_new();
  SP_GC_ROOT(a);
  sp_StrArray_push(a, sp_file_dirname(path));
  sp_StrArray_push(a, sp_file_basename(path));
  return a;
}
mrb_bool sp_file_zero(const char *path) {
  struct stat st;
  if (stat(path ? path : "", &st) != 0) return 0;
  if (S_ISDIR(st.st_mode)) return 0;
  return st.st_size == 0;
}
const char *sp_file_dirname(const char *path) {
  const char *s = strrchr(path, '/');
  if (!s) { char *r = sp_str_alloc(1); r[0] = '.'; r[1] = 0; return r; }
  if (s == path) { char *r = sp_str_alloc(1); r[0] = '/'; r[1] = 0; return r; }
  size_t n = (size_t)(s - path);
  char *buf = sp_str_alloc(n);
  memcpy(buf, path, n); buf[n] = 0;
  return buf;
}
const char *sp_dir_pwd(void) {
  char tmp[4096];
  if (!getcwd(tmp, sizeof(tmp))) { return sp_str_empty; }
  size_t n = strlen(tmp);
  char *buf = sp_str_alloc(n);
  memcpy(buf, tmp, n + 1);
  return buf;
}
mrb_int sp_dir_mkdir(const char *path) {
  return (mrb_int)mkdir(path, 0777);
}
mrb_int sp_dir_rmdir(const char *path) {
  return (mrb_int)rmdir(path);
}
mrb_int sp_dir_chdir(const char *path) {
  return (mrb_int)chdir(path);
}
const char *sp_dir_home(void) {
  const char *h = getenv("HOME");
  if (!h) return sp_str_empty;
  return sp_str_dup_external(h);
}
void sp_Dir_fin(void *p) { sp_Dir *d = (sp_Dir *)p; if (d->dp) { closedir(d->dp); d->dp = NULL; } }
void sp_Dir_scan(void *p) { sp_Dir *d = (sp_Dir *)p; if (d->path) sp_mark_string(d->path); }
sp_Dir *sp_Dir_new(const char *path) {
  DIR *dp = opendir(path ? path : "");
  if (!dp)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ dir_initialize - %s", path ? path : ""));
  sp_Dir *d = (sp_Dir *)sp_gc_alloc(sizeof(sp_Dir), sp_Dir_fin, sp_Dir_scan);
  d->dp = dp;
  d->path = NULL;   /* set below: the sprintf may GC, and the scan reads path */
  SP_GC_ROOT(d);
  d->path = sp_sprintf("%s", path ? path : "");
  return d;
}
const char *sp_Dir_read(sp_Dir *d) {
  if (!d || !d->dp) return NULL;
  struct dirent *e = readdir(d->dp);
  return e ? sp_sprintf("%s", e->d_name) : NULL;
}
const char *sp_Dir_path(sp_Dir *d) { return d && d->path ? d->path : sp_str_empty; }
sp_RbVal sp_Dir_close(sp_Dir *d) { if (d && d->dp) { closedir(d->dp); d->dp = NULL; } return sp_box_nil(); }
sp_Dir *sp_Dir_rewind(sp_Dir *d) { if (d && d->dp) rewinddir(d->dp); return d; }
mrb_int sp_Dir_tell(sp_Dir *d) { return d && d->dp ? (mrb_int)telldir(d->dp) : 0; }
sp_StrArray *sp_dir_entries(const char *path) { return sp_dir_entries_impl(path, 0); }
mrb_bool sp_dir_empty(const char *path) {
  struct stat st;
  if (!path || stat(path, &st) != 0)
    sp_raise_cls("Errno::ENOENT",
                 sp_sprintf("No such file or directory @ rb_dir_s_empty_p - %s", path ? path : ""));
  if (!S_ISDIR(st.st_mode)) return FALSE;
  DIR *d = opendir(path);
  if (!d) return FALSE;
  struct dirent *e; mrb_bool empty = TRUE;
  while ((e = readdir(d))) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    empty = FALSE; break;
  }
  closedir(d);
  return empty;
}
const char *sp_dir_home_user(const char *user) {
  if (!user || !*user) return sp_dir_home();
  struct passwd *pw = getpwnam(user);
  if (!pw || !pw->pw_dir)
    sp_raise_cls("ArgumentError", sp_sprintf("user %s doesn't exist", user));
  return sp_str_dup_external(pw->pw_dir);
}
sp_StrArray *sp_dir_children(const char *path) { return sp_dir_entries_impl(path, 1); }

/* ---- Signal trap machinery + Enumerator cursor/generator ops moved from
   sp_runtime.h ----
   Only tiny TU-defined helpers are linked from here (sp_proc_call, the boxed
   proc constructor, the trap state, the proc argument/return slots): the heavy
   poly/hash/render helpers (sp_enum_items_from, sp_poly_each_elem, ...) stay
   `static` in the generated TU so -O1 still prunes them from programs that
   never use them -- de-externing those measurably bloats every test compile. */
#include <signal.h>
#include "sp_enum.h"
typedef struct sp_Proc sp_Proc;
extern char **environ;
extern sp_RbVal sp_box_proc(void *p);
extern const char *sp_trap_state[];
extern struct sp_Proc *sp_trap_proc[];
extern SP_NORETURN void sp_raise_stop_iteration(sp_RbVal result);
extern SP_TLS sp_RbVal _sp_proc_poly_ret;
extern SP_TLS sp_RbVal _sp_proc_poly_args[];
extern mrb_int sp_proc_call(sp_Proc *p, mrb_int argc, mrb_int *args);
extern const char *sp_signal_signame(mrb_int no);
extern SP_COLD int sp_signal_resolve(sp_RbVal sig);

sp_Enumerator *sp_Enumerator_dup(sp_Enumerator *e);
void sp_Enumerator_scan(void *p);
sp_Enumerator *sp_enum_with_src(sp_Enumerator *e, sp_RbVal src, const char *meth);
sp_Enumerator *sp_enum_as_gen(sp_Enumerator *e);
sp_RbVal sp_enum_with_index_value(sp_Enumerator *e);
sp_RbVal sp_enum_with_index_result(sp_Enumerator *e, sp_PolyArray *mapped);
sp_Enumerator *sp_Enumerator_new_from_items(sp_PolyArray *items);
sp_Enumerator *sp_Enumerator_with_index(sp_Enumerator *e, mrb_int off);
sp_Enumerator *sp_Enumerator_new_gen(void (*gen)(sp_Fiber *), void *cap, sp_RbVal size);
sp_RbVal sp_enum_gen_pull(sp_Enumerator *e);
sp_RbVal sp_Enumerator_next(sp_Enumerator *e);
sp_RbVal sp_Enumerator_peek(sp_Enumerator *e);
sp_PolyArray *sp_enum_values_wrap(sp_RbVal v);
sp_PolyArray *sp_Enumerator_next_values(sp_Enumerator *e);
sp_PolyArray *sp_Enumerator_peek_values(sp_Enumerator *e);
sp_Enumerator *sp_Enumerator_rewind(sp_Enumerator *e);
sp_RbVal sp_Enumerator_feed(sp_Enumerator *e, sp_RbVal v);
sp_PolyArray *sp_Enumerator_take(sp_Enumerator *e, mrb_int n);
sp_PolyArray *sp_Enumerator_to_a(sp_Enumerator *e);
void sp_sig_c_handler(int no);
void sp_sig_exit_dispatch(void);
sp_RbVal sp_signal_trap(sp_RbVal sig, sp_RbVal handler);
mrb_int sp_process_kill1(sp_RbVal sig, mrb_int pid);
sp_RbVal sp_Enumerator_size(sp_Enumerator *e);

sp_Enumerator *sp_Enumerator_dup(sp_Enumerator *e) {
  if (!e) return e;
  sp_Enumerator *d = (sp_Enumerator *)sp_gc_alloc(sizeof(sp_Enumerator), NULL, sp_Enumerator_scan);
  *d = *e;
  return d;
}
void sp_Enumerator_scan(void *p) {
  sp_Enumerator *e = (sp_Enumerator *)p;
  if (e->items) sp_gc_mark(e->items);
  if (e->fib) sp_gc_mark(e->fib);
  if (e->gen_cap) sp_gc_mark(e->gen_cap);
  if (e->peeked) sp_mark_rbval(e->peek_val);
  sp_mark_rbval(e->size);
  if (e->has_feed) sp_mark_rbval(e->feed);
  sp_mark_rbval(e->gen_result);
  sp_mark_rbval(e->source);
}
sp_Enumerator *sp_enum_with_src(sp_Enumerator *e, sp_RbVal src, const char *meth) {
  e->source = src;
  e->meth = meth;
  return e;
}
sp_Enumerator *sp_enum_as_gen(sp_Enumerator *e) {
  e->gen_label = TRUE;
  return e;
}
sp_RbVal sp_enum_with_index_value(sp_Enumerator *e) {
  if (e->meth && (strcmp(e->meth, "each") == 0 || strcmp(e->meth, "each_with_index") == 0))
    return e->source;
  sp_raise_cls("NotImplementedError",
               sp_sprintf("Enumerator#with_index return value for a stored %s enumerator",
                          e->meth ? e->meth : "generator"));
  return sp_box_nil();
}
sp_RbVal sp_enum_with_index_result(sp_Enumerator *e, sp_PolyArray *mapped) {
  if (e->meth && (strcmp(e->meth, "map") == 0 || strcmp(e->meth, "collect") == 0))
    return sp_box_poly_array(mapped);
  if (e->meth && (strcmp(e->meth, "each") == 0 || strcmp(e->meth, "each_with_index") == 0))
    return e->source;
  sp_raise_cls("NotImplementedError",
               sp_sprintf("Enumerator#with_index return value for a stored %s enumerator",
                          e->meth ? e->meth : "generator"));
  return sp_box_nil();
}
sp_Enumerator *sp_Enumerator_new_from_items(sp_PolyArray *items) {
  SP_GC_ROOT(items);
  sp_Enumerator *e = (sp_Enumerator *)sp_gc_alloc(sizeof(sp_Enumerator), NULL, sp_Enumerator_scan);
  e->items = items; e->cursor = 0; e->gen = NULL; e->gen_cap = NULL; e->fib = NULL; e->peeked = FALSE; e->size = sp_box_nil(); e->feed = sp_box_nil(); e->has_feed = FALSE; e->gen_result = sp_box_nil(); e->source = sp_box_nil(); e->meth = "each";
  return e;
}
sp_Enumerator *sp_Enumerator_with_index(sp_Enumerator *e, mrb_int off) {
  sp_PolyArray *src = e ? e->items : NULL;
  mrb_int n = src ? src->len : 0;
  /* Root the source enumerator (a temp `arr.each` is otherwise unreachable): the
     sp_PolyArray_new below can collect it and free src mid-loop -> use-after-free
     of src->data[i] under GC stress. Rooting `e` keeps src alive transitively --
     sp_Enumerator_scan marks e->items and the GC is non-moving. */
  SP_GC_ROOT(e);
  sp_PolyArray *out = sp_PolyArray_new(); SP_GC_ROOT(out);
  for (mrb_int i = 0; i < n; i++) {
    sp_PolyArray *pair = sp_PolyArray_new(); SP_GC_ROOT(pair);
    sp_PolyArray_push(pair, src->data[i]);
    sp_PolyArray_push(pair, sp_box_int(off + i));
    sp_PolyArray_push(out, sp_box_poly_array(pair));
  }
  { sp_RbVal src_recv = e ? e->source : sp_box_nil();
    sp_Enumerator *r = sp_Enumerator_new_from_items(out); r->source = src_recv; return r; }
}
sp_Enumerator *sp_Enumerator_new_gen(void (*gen)(sp_Fiber *), void *cap, sp_RbVal size) {
  sp_Enumerator *e = (sp_Enumerator *)sp_gc_alloc(sizeof(sp_Enumerator), NULL, sp_Enumerator_scan);
  e->items = NULL; e->cursor = 0; e->gen = gen; e->gen_cap = cap; e->fib = NULL; e->peeked = FALSE; e->size = size; e->feed = sp_box_nil(); e->has_feed = FALSE; e->gen_result = sp_box_nil(); e->source = sp_box_nil(); e->meth = "each";
  return e;
}
sp_RbVal sp_enum_gen_pull(sp_Enumerator *e) {
  if (!e->fib) {
    e->fib = sp_Fiber_new(e->gen);
    if (e->gen_cap) e->fib->user_data = e->gen_cap;
  }
  if (!sp_Fiber_alive(e->fib)) sp_raise_stop_iteration(e->gen_result);
  sp_RbVal feed = e->has_feed ? e->feed : sp_box_nil();
  e->has_feed = FALSE; e->feed = sp_box_nil();   /* consumed by this resume */
  sp_RbVal v = sp_Fiber_resume(e->fib, feed);
  if (!sp_Fiber_alive(e->fib)) { e->gen_result = v; sp_raise_stop_iteration(v); }
  return v;
}
sp_RbVal sp_Enumerator_next(sp_Enumerator *e) {
  if (e->gen) {
    if (e->peeked) { e->peeked = FALSE; return e->peek_val; }
    return sp_enum_gen_pull(e);
  }
  if (!e->items || e->cursor >= e->items->len) sp_raise_stop_iteration(e->source);
  return e->items->data[e->cursor++];
}
sp_RbVal sp_Enumerator_peek(sp_Enumerator *e) {
  if (e->gen) {
    if (!e->peeked) { e->peek_val = sp_enum_gen_pull(e); e->peeked = TRUE; }
    return e->peek_val;
  }
  if (!e->items || e->cursor >= e->items->len) sp_raise_stop_iteration(e->source);
  return e->items->data[e->cursor];
}
sp_PolyArray *sp_enum_values_wrap(sp_RbVal v) {
  if (v.tag == SP_TAG_OBJ && v.cls_id == SP_BUILTIN_POLY_ARRAY) return (sp_PolyArray *)v.v.p;
  sp_PolyArray *a = sp_PolyArray_new(); SP_GC_ROOT(a);
  sp_PolyArray_push(a, v);
  return a;
}
sp_PolyArray *sp_Enumerator_next_values(sp_Enumerator *e) { return sp_enum_values_wrap(sp_Enumerator_next(e)); }
sp_PolyArray *sp_Enumerator_peek_values(sp_Enumerator *e) { return sp_enum_values_wrap(sp_Enumerator_peek(e)); }
sp_Enumerator *sp_Enumerator_rewind(sp_Enumerator *e) {
  if (!e) return NULL;
  if (e->gen) { e->fib = NULL; e->peeked = FALSE; e->gen_result = sp_box_nil(); }
  else e->cursor = 0;
  e->feed = sp_box_nil(); e->has_feed = FALSE;
  return e;
}
sp_RbVal sp_Enumerator_feed(sp_Enumerator *e, sp_RbVal v) {
  if (!e) return sp_box_nil();
  if (e->has_feed) sp_raise_cls("TypeError", (&("\xff" "feed value already set")[1]));
  e->feed = v; e->has_feed = TRUE;
  return sp_box_nil();
}
sp_PolyArray *sp_Enumerator_take(sp_Enumerator *e, mrb_int n) {
  sp_PolyArray *r = sp_PolyArray_new();
  SP_GC_ROOT(r);
  if (n <= 0) return r;
  if (e->gen) {
    sp_Fiber *f = sp_Fiber_new(e->gen);
    SP_GC_ROOT(f);
    if (e->gen_cap) f->user_data = e->gen_cap;
    for (mrb_int i = 0; i < n; i++) {
      if (!sp_Fiber_alive(f)) break;
      sp_RbVal v = sp_Fiber_resume(f, sp_box_nil());
      if (!sp_Fiber_alive(f)) break;
      sp_PolyArray_push(r, v);
    }
    return r;
  }
  mrb_int lim = e->items ? e->items->len : 0;
  if (n < lim) lim = n;
  for (mrb_int i = 0; i < lim; i++) sp_PolyArray_push(r, e->items->data[i]);
  return r;
}
sp_PolyArray *sp_Enumerator_to_a(sp_Enumerator *e) {
  sp_PolyArray *r = sp_PolyArray_new();
  SP_GC_ROOT(r);
  if (!e) return r;
  if (e->gen) {
    sp_Fiber *f = sp_Fiber_new(e->gen);
    SP_GC_ROOT(f);
    if (e->gen_cap) f->user_data = e->gen_cap;
    while (sp_Fiber_alive(f)) {
      sp_RbVal v = sp_Fiber_resume(f, sp_box_nil());
      if (!sp_Fiber_alive(f)) break;
      sp_PolyArray_push(r, v);
    }
    return r;
  }
  if (e->items) for (mrb_int i = 0; i < e->items->len; i++) sp_PolyArray_push(r, e->items->data[i]);
  return r;
}
void sp_sig_c_handler(int no) {
  sp_Proc *p = (no >= 0 && no < SP_SIG_MAX) ? (sp_Proc *)sp_trap_proc[no] : NULL;
  if (p) {
    mrb_int slot = (mrb_int)no;
    _sp_proc_poly_args[0] = sp_box_int((mrb_int)no);
    sp_proc_call(p, 1, &slot);
  }
}
void sp_sig_exit_dispatch(void) {
  sp_Proc *p = (sp_Proc *)sp_trap_proc[0];
  sp_trap_proc[0] = NULL;   /* run once */
  if (p) { mrb_int slot = 0; _sp_proc_poly_args[0] = sp_box_int(0); sp_proc_call(p, 1, &slot); }
}
sp_RbVal sp_signal_trap(sp_RbVal sig, sp_RbVal handler) {
  int no = sp_signal_resolve(sig);
  if (no == SIGKILL || no == SIGSTOP)
    sp_raise_cls("Errno::EINVAL",
                 sp_sprintf("Invalid argument - SIG%s", sp_signal_signame(no)));
  /* the previous handler: a stored proc or command string; an untouched
     signal reads "DEFAULT", except EXIT whose default handler is nil (#2839) */
  sp_RbVal prev = sp_trap_proc[no] ? sp_box_proc((sp_Proc *)sp_trap_proc[no])
                : sp_trap_state[no] ? sp_box_str(sp_trap_state[no])
                : no == 0 ? sp_box_nil()
                : sp_box_str((&("\xff" "DEFAULT")[1]));
  if (handler.tag == SP_TAG_OBJ && handler.cls_id == SP_BUILTIN_PROC && handler.v.p) {
    sp_trap_proc[no] = (sp_Proc *)handler.v.p;
    sp_trap_state[no] = NULL;
    if (no == 0) { static int armed = 0; if (!armed) { armed = 1; atexit(sp_sig_exit_dispatch); } }
    else signal(no, sp_sig_c_handler);
  }
  else {
    const char *hs = (handler.tag == SP_TAG_STR && handler.v.s) ? handler.v.s : "DEFAULT";
    int ignore = strcmp(hs, "IGNORE") == 0 || strcmp(hs, "SIG_IGN") == 0;
    sp_trap_proc[no] = NULL;
    /* SYSTEM_DEFAULT reads back as itself (#2839); it installs SIG_DFL too.
       An EXIT string command clears the handler, whose read-back is nil. */
    sp_trap_state[no] = no == 0 ? NULL
                      : ignore ? (&("\xff" "IGNORE")[1])
                      : strcmp(hs, "SYSTEM_DEFAULT") == 0 ? (&("\xff" "SYSTEM_DEFAULT")[1])
                      : (&("\xff" "DEFAULT")[1]);
    if (no != 0) signal(no, ignore ? SIG_IGN : SIG_DFL);
  }
  return prev;
}
mrb_int sp_process_kill1(sp_RbVal sig, mrb_int pid) {
  int no = sp_signal_resolve(sig);
  if (kill((pid_t)pid, no) != 0) {
    if (errno == ESRCH) sp_raise_cls("Errno::ESRCH", sp_sprintf("No such process - %lld", (long long)pid));
    if (errno == EPERM) sp_raise_cls("Errno::EPERM", sp_sprintf("Operation not permitted - %lld", (long long)pid));
    sp_raise_cls("Errno::EINVAL", "Invalid argument");
  }
  return 1;
}
sp_RbVal sp_Enumerator_size(sp_Enumerator *e) {
  if (!e) return sp_box_nil();
  if (e->items) return sp_box_int(e->items->len);
  if (e->size.tag == SP_TAG_OBJ && e->size.cls_id == SP_BUILTIN_PROC) {
    (void)sp_proc_call((sp_Proc *)e->size.v.p, 0, NULL);
    return _sp_proc_poly_ret;
  }
  return e->size;
}

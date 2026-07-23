#include <stdio.h>
#include <stdarg.h>
#include "libc_internal.h"

/* =====================================================================
 * printf/scanf formatting engine (Phase 4c core).  _vformat drives every
 * printf-family entry point over a _psink that is either a FILE
 * (printf/fprintf) or a bounded buffer (snprintf).  The %f/%e/%g paths pull
 * the soft-float floord/fpdtol from libm.  Kept in one object so the thin
 * public wrappers strip independently.
 * ===================================================================== */
static void _emit(_psink *s, int c) {
  if (s->fp)
    fputc(c, s->fp);
  else if (s->buf && s->len + 1 < s->cap)
    s->buf[s->len] = (char)c;
  s->len++;
}

static int _u64toa(unsigned long long v, int base, int upper, char *out) {
  const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char tmp[24];
  int n = 0;
  do {
    tmp[n++] = digs[v % base];
    v /= base;
  } while (v);
  for (int i = 0; i < n; i++)
    out[i] = tmp[n - 1 - i];
  out[n] = 0;
  return n;
}

/* Fixed-point (%f): format v >= 0 with `prec` fractional digits. The integer
   part is assumed to fit a 32-bit long (fpdtol truncates). */
static int fmt_fixed(double v, int prec, char *buf) {
  double r = 0.5;
  for (int i = 0; i < prec; i++)
    r = r / 10.0;
  v = v + r; /* round */
  double ip = floord(v);
  double frac = v - ip;
  int n = 0;
  long li = fpdtol(ip);
  char tmp[16];
  int t = 0;
  if (li == 0)
    tmp[t++] = '0';
  while (li > 0) {
    tmp[t++] = (char)('0' + (int)(li % 10));
    li = li / 10;
  }
  while (t > 0)
    buf[n++] = tmp[--t];
  if (prec > 0) {
    buf[n++] = '.';
    for (int i = 0; i < prec; i++) {
      frac = frac * 10.0;
      double d = floord(frac);
      buf[n++] = (char)('0' + (int)fpdtol(d));
      frac = frac - d;
    }
  }
  buf[n] = 0;
  return n;
}

/* Scientific (%e): d.ddde+XX with `prec` fractional digits. */
static int fmt_sci(double v, int prec, char *buf) {
  int exp = 0;
  if (v != 0.0) {
    while (v >= 10.0) {
      v = v / 10.0;
      exp++;
    }
    while (v < 1.0) {
      v = v * 10.0;
      exp--;
    }
  }
  int n = fmt_fixed(v, prec, buf);
  buf[n++] = 'e';
  buf[n++] = (char)(exp < 0 ? '-' : '+');
  int ae = exp < 0 ? -exp : exp;
  buf[n++] = (char)('0' + (ae / 10) % 10);
  buf[n++] = (char)('0' + ae % 10);
  buf[n] = 0;
  return n;
}

/* General (%g): pick %e for very large/small magnitudes, else %f. */
static int fmt_gen(double v, int prec, char *buf) {
  if (prec <= 0)
    prec = 1;
  int exp = 0;
  double t = v;
  if (t != 0.0) {
    while (t >= 10.0) {
      t = t / 10.0;
      exp++;
    }
    while (t < 1.0) {
      t = t * 10.0;
      exp--;
    }
  }
  if (exp < -4 || exp >= prec)
    return fmt_sci(v, prec - 1, buf);
  int fp = prec - 1 - exp;
  return fmt_fixed(v, fp > 0 ? fp : 0, buf);
}

int _vformat(_psink *s, const char *fmt, va_list ap) {
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      _emit(s, (unsigned char)*fmt);
      continue;
    }
    fmt++;

    int left = 0, zero = 0, plus = 0, space = 0;
    for (;; fmt++) {
      if (*fmt == '-')
        left = 1;
      else if (*fmt == '0')
        zero = 1;
      else if (*fmt == '+')
        plus = 1;
      else if (*fmt == ' ')
        space = 1;
      else
        break;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9')
      width = width * 10 + (*fmt++ - '0');
    int prec = -1;
    if (*fmt == '.') {
      fmt++;
      prec = 0;
      while (*fmt >= '0' && *fmt <= '9')
        prec = prec * 10 + (*fmt++ - '0');
    }
    int lng = 0;
    while (*fmt == 'l') {
      lng++;
      fmt++;
    }
    while (*fmt == 'h')
      fmt++;

    char numbuf[64];
    const char *str = numbuf;
    int slen = 0;
    char sign = 0;

    switch (*fmt) {
    case 'd':
    case 'i': {
      long long v = (lng >= 2) ? va_arg(ap, long long) : va_arg(ap, long);
      unsigned long long uv;
      if (v < 0) {
        sign = '-';
        uv = (unsigned long long)(-v);
      } else {
        uv = (unsigned long long)v;
        sign = plus ? '+' : (space ? ' ' : 0);
      }
      slen = _u64toa(uv, 10, 0, numbuf);
      break;
    }
    case 'u':
    case 'x':
    case 'X':
    case 'o': {
      unsigned long long uv =
          (lng >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
      int base = (*fmt == 'x' || *fmt == 'X') ? 16 : (*fmt == 'o') ? 8 : 10;
      slen = _u64toa(uv, base, *fmt == 'X', numbuf);
      break;
    }
    case 'c':
      numbuf[0] = (char)va_arg(ap, int);
      numbuf[1] = 0;
      slen = 1;
      break;
    case 's':
      str = va_arg(ap, const char *);
      if (!str)
        str = "(null)";
      while (str[slen] && (prec < 0 || slen < prec))
        slen++;
      break;
    case 'p': {
      unsigned long uv = (unsigned long)va_arg(ap, void *);
      numbuf[0] = '0';
      numbuf[1] = 'x';
      slen = _u64toa(uv, 16, 0, numbuf + 2) + 2;
      break;
    }
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G': {
      double dv = va_arg(ap, double);
      int p = (prec < 0) ? 6 : prec;
      /* Non-finite (inf/NaN) must be caught from the raw IEEE bits BEFORE the
         numeric path: the %e/%g normalization loop `while (v >= 10.0) v /= 10`
         never terminates for +inf (inf/10 == inf), and the soft-float compares
         misbehave on NaN.  Emit inf/nan (upper-case for the F/E/G spellings). */
      {
        union {
          double d;
          struct {
            unsigned long hi, lo;
          } w;
        } u;
        u.d = dv;
        if (((u.w.hi >> 20) & 0x7FF) == 0x7FF) {
          int is_nan = ((u.w.hi & 0xFFFFF) | u.w.lo) != 0;
          int up = (*fmt == 'F' || *fmt == 'E' || *fmt == 'G');
          const char *w = is_nan ? (up ? "NAN" : "nan") : (up ? "INF" : "inf");
          numbuf[0] = w[0];
          numbuf[1] = w[1];
          numbuf[2] = w[2];
          numbuf[3] = 0;
          slen = 3;
          sign = is_nan
                     ? 0
                     : ((u.w.hi >> 31) ? '-' : (plus ? '+' : (space ? ' ' : 0)));
          break;
        }
      }
      if (dv < 0.0) {
        sign = '-';
        dv = -dv;
      } else {
        sign = plus ? '+' : (space ? ' ' : 0);
      }
      if (*fmt == 'e' || *fmt == 'E')
        slen = fmt_sci(dv, p, numbuf);
      else if (*fmt == 'g' || *fmt == 'G')
        slen = fmt_gen(dv, p, numbuf);
      else
        slen = fmt_fixed(dv, p, numbuf);
      break;
    }
    case '%':
      _emit(s, '%');
      continue;
    default:
      _emit(s, '%');
      _emit(s, (unsigned char)*fmt);
      continue;
    }

    int total = slen + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;
    if (!left && !zero)
      while (pad-- > 0)
        _emit(s, ' ');
    if (sign)
      _emit(s, sign);
    if (!left && zero)
      while (pad-- > 0)
        _emit(s, '0');
    for (int i = 0; i < slen; i++)
      _emit(s, (unsigned char)str[i]);
    if (left)
      while (pad-- > 0)
        _emit(s, ' ');
  }
  return s->len;
}

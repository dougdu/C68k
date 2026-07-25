#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <string.h>
#include "libc_internal.h"

/* =====================================================================
 * Wide formatted output (Tier B): _vwformat mirrors _vformat over a wchar_t
 * sink (a wide-oriented FILE via fputwc, or a wchar_t buffer).  Numeric and
 * float conversions reuse the narrow _fmt_* helpers from vformat.c (they
 * produce ASCII) and widen each byte; only %c/%s carry the wide-specific
 * argument semantics (%c/%s take a byte/multibyte string, %lc/%ls a
 * wchar_t/wide string).  Kept in its own object so narrow-only printf
 * programs never link the wide engine (dead-stripping).
 * ===================================================================== */
static void _wemit(_pwsink *s, wchar_t c) {
  if (s->fp)
    fputwc(c, s->fp);
  else if (s->buf && s->len + 1 < s->cap)
    s->buf[s->len] = c;
  s->len++;
}

/* field-padded emit of a run of `n` wide characters (used by %c and %ls). */
static void _wfield(_pwsink *s, const wchar_t *w, int n, int width, int left) {
  int pad = width > n ? width - n : 0;
  if (!left)
    while (pad-- > 0)
      _wemit(s, L' ');
  for (int i = 0; i < n; i++)
    _wemit(s, w[i]);
  if (left)
    while (pad-- > 0)
      _wemit(s, L' ');
}

/* field-padded emit of a multibyte (UTF-8) string transcoded to wide, at most
   `prec` wide characters (prec < 0 = the whole string).  Two passes over the
   same bytes (count, then emit) each with a fresh conversion state. */
static void _wfield_mb(_pwsink *s, const char *m, int prec, int width, int left) {
  mbstate_t st;
  int n = 0;
  memset(&st, 0, sizeof st);
  for (const char *p = m; *p && (prec < 0 || n < prec);) {
    wchar_t wc;
    size_t r = mbrtowc(&wc, p, 4, &st);
    if (r == (size_t)-1 || r == (size_t)-2)
      break;
    if (r == 0)
      r = 1;
    p += r;
    n++;
  }
  int pad = width > n ? width - n : 0;
  if (!left)
    while (pad-- > 0)
      _wemit(s, L' ');
  memset(&st, 0, sizeof st);
  int c = 0;
  for (const char *p = m; *p && (prec < 0 || c < prec);) {
    wchar_t wc;
    size_t r = mbrtowc(&wc, p, 4, &st);
    if (r == (size_t)-1 || r == (size_t)-2)
      break;
    if (r == 0) {
      wc = 0;
      r = 1;
    }
    _wemit(s, wc);
    p += r;
    c++;
  }
  if (left)
    while (pad-- > 0)
      _wemit(s, L' ');
}

int _vwformat(_pwsink *s, const wchar_t *fmt, va_list ap) {
  for (; *fmt; fmt++) {
    if (*fmt != L'%') {
      _wemit(s, *fmt);
      continue;
    }
    fmt++;

    int left = 0, zero = 0, plus = 0, space = 0;
    for (;; fmt++) {
      if (*fmt == L'-')
        left = 1;
      else if (*fmt == L'0')
        zero = 1;
      else if (*fmt == L'+')
        plus = 1;
      else if (*fmt == L' ')
        space = 1;
      else
        break;
    }
    int width = 0;
    while (*fmt >= L'0' && *fmt <= L'9')
      width = width * 10 + (int)(*fmt++ - L'0');
    int prec = -1;
    if (*fmt == L'.') {
      fmt++;
      prec = 0;
      while (*fmt >= L'0' && *fmt <= L'9')
        prec = prec * 10 + (int)(*fmt++ - L'0');
    }
    int lng = 0;
    while (*fmt == L'l') {
      lng++;
      fmt++;
    }
    int hsh = 0;
    while (*fmt == L'h') {
      hsh++;
      fmt++;
    }

    char numbuf[64];
    int slen = 0;
    char sign = 0;

    switch (*fmt) {
    case L'd':
    case L'i': {
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
    case L'u':
    case L'x':
    case L'X':
    case L'o': {
      unsigned long long uv =
          (lng >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
      int base = (*fmt == L'x' || *fmt == L'X') ? 16 : (*fmt == L'o') ? 8 : 10;
      slen = _u64toa(uv, base, *fmt == L'X', numbuf);
      break;
    }
    case L'c': {
      wchar_t wc = (lng >= 1) ? (wchar_t)va_arg(ap, wint_t)
                              : (wchar_t)(unsigned char)va_arg(ap, int);
      _wfield(s, &wc, 1, width, left);
      continue;
    }
    case L's':
      if (lng >= 1) {
        const wchar_t *w = va_arg(ap, const wchar_t *);
        if (!w)
          w = L"(null)";
        int n = 0;
        while (w[n] && (prec < 0 || n < prec))
          n++;
        _wfield(s, w, n, width, left);
      } else {
        const char *m = va_arg(ap, const char *);
        if (!m)
          m = "(null)";
        _wfield_mb(s, m, prec, width, left);
      }
      continue;
    case L'p': {
      unsigned long uv = (unsigned long)va_arg(ap, void *);
      numbuf[0] = '0';
      numbuf[1] = 'x';
      slen = _u64toa(uv, 16, 0, numbuf + 2) + 2;
      break;
    }
    case L'f':
    case L'F':
    case L'e':
    case L'E':
    case L'g':
    case L'G':
    case L'a':
    case L'A': {
      double dv = va_arg(ap, double);
      int p = (prec < 0) ? 6 : prec;
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
          int up = (*fmt == L'F' || *fmt == L'E' || *fmt == L'G' || *fmt == L'A');
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
      if (*fmt == L'a' || *fmt == L'A')
        slen = _fmt_hex(dv, prec, *fmt == L'A', numbuf);
      else if (*fmt == L'e' || *fmt == L'E')
        slen = _fmt_sci(dv, p, numbuf);
      else if (*fmt == L'g' || *fmt == L'G')
        slen = _fmt_gen(dv, p, numbuf);
      else
        slen = _fmt_fixed(dv, p, numbuf);
      break;
    }
    case L'n': {
      int written = s->len;
      if (lng >= 2)
        *va_arg(ap, long long *) = written;
      else if (lng == 1)
        *va_arg(ap, long *) = written;
      else if (hsh >= 2)
        *va_arg(ap, signed char *) = (signed char)written;
      else if (hsh == 1)
        *va_arg(ap, short *) = (short)written;
      else
        *va_arg(ap, int *) = written;
      continue;
    }
    case L'%':
      _wemit(s, L'%');
      continue;
    default:
      _wemit(s, L'%');
      _wemit(s, *fmt);
      continue;
    }

    /* numeric/float conversions: pad (sign + zero/space fill) then widen the
       ASCII digits produced by the shared _fmt_* helpers. */
    int total = slen + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;
    if (!left && !zero)
      while (pad-- > 0)
        _wemit(s, L' ');
    if (sign)
      _wemit(s, (wchar_t)sign);
    if (!left && zero)
      while (pad-- > 0)
        _wemit(s, L'0');
    for (int i = 0; i < slen; i++)
      _wemit(s, (wchar_t)(unsigned char)numbuf[i]);
    if (left)
      while (pad-- > 0)
        _wemit(s, L' ');
  }
  return s->len;
}

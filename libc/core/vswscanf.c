#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "libc_internal.h"

/* =====================================================================
 * Wide formatted input (Tier C): _vwscan mirrors the narrow _vscan
 * (vsscanf.c) over a wchar_t source -- either a wide-oriented FILE (read via
 * fgetwc with one wide char of pushback through ungetwc) or a NUL-terminated
 * wchar_t string (swscanf).  Numeric tokens are ASCII, so each accepted wide
 * character is narrowed into a char token buffer and handed to the same
 * strtoll/strtoull/strtod primitives the narrow scanner uses.  Only the
 * string conversions carry wide-specific argument semantics: %c/%s/%[ store a
 * multibyte (UTF-8) char array by default and a wchar_t array under the l
 * length modifier (%lc/%ls/%l[).  Kept in its own object so narrow-only
 * scanf programs never link the wide engine (dead-stripping).
 * ===================================================================== */

#define WSET_WIDE 32 /* non-ASCII scanset members held per %[ conversion */

/* Fetch the next input wide character (consumed), or WEOF. */
static wint_t wsc_get(_wscan *z) {
  wint_t c;
  if (z->fp) {
    c = fgetwc(z->fp);
  } else {
    wchar_t u = *z->s;
    if (!u)
      return WEOF;
    z->s++;
    c = (wint_t)u;
  }
  if (c != WEOF)
    z->nread++;
  return c;
}

/* Push the last-read wide character back so the next wsc_get returns it. */
static void wsc_unget(_wscan *z, wint_t c) {
  if (c == WEOF)
    return;
  z->nread--;
  if (z->fp)
    ungetwc(c, z->fp);
  else
    z->s--;
}

/* First non-whitespace wide character (consumed), or WEOF. */
static wint_t wsc_skipws(_wscan *z) {
  wint_t c;
  do {
    c = wsc_get(z);
  } while (c != WEOF && iswspace(c));
  return c;
}

/* Is wide c a valid digit in the given base (2..16)?  Safe for any code point
 * (a non-ASCII wc never satisfies the ASCII digit/letter ranges). */
static int wdigit_ok(wint_t c, int base) {
  int d;
  if (c >= L'0' && c <= L'9')
    d = (int)(c - L'0');
  else {
    wint_t lc = c | 0x20;
    if (lc < L'a' || lc > L'z')
      return 0;
    d = (int)(lc - L'a') + 10;
  }
  return d < base;
}

/* Is wide c a hexadecimal digit?  (guards the narrow isxdigit against wide
 * values that would index out of its table). */
static int wxdigit(wint_t c) {
  if (c >= L'0' && c <= L'9')
    return 1;
  wint_t lc = c | 0x20;
  return lc >= L'a' && lc <= L'f';
}

extern double ldexp(double x, int n);

/* Convert a hex-float token "[+/-]0x h.hh [p[+/-]d]" (ASCII, already
 * accumulated) to double without strtod: build the mantissa as a double and
 * scale by a power of two.  A private copy of the narrow scanner's helper so
 * the wide engine stays self-contained (dead-strippable). */
static double w_hexfloat(const char *s) {
  int neg = 0;
  if (*s == '+')
    s++;
  else if (*s == '-') {
    neg = 1;
    s++;
  }
  s += 2; /* skip the "0x" / "0X" prefix */
  double mant = 0.0;
  int fracdig = 0, seendot = 0;
  for (; *s; s++) {
    int ch = *s;
    if (ch == '.') {
      seendot = 1;
      continue;
    }
    int d;
    if (ch >= '0' && ch <= '9')
      d = ch - '0';
    else {
      int lc = ch | 0x20;
      if (lc < 'a' || lc > 'f')
        break; /* the 'p' exponent (or the end of the token) */
      d = lc - 'a' + 10;
    }
    mant = mant * 16.0 + (double)d;
    if (seendot)
      fracdig++;
  }
  int binexp = 0, esign = 1;
  if (*s == 'p' || *s == 'P') {
    s++;
    if (*s == '+')
      s++;
    else if (*s == '-') {
      esign = -1;
      s++;
    }
    while (*s >= '0' && *s <= '9')
      binexp = binexp * 10 + (*s++ - '0');
    binexp *= esign;
  }
  double v = ldexp(mant, binexp - 4 * fracdig);
  return neg ? -v : v;
}

int _vwscan(_wscan *z, const wchar_t *fmt, va_list ap) {
  int count = 0;
  int eof_hit = 0;
  wint_t c;
  char tok[64];

  for (; *fmt; fmt++) {
    /* whitespace directive: match zero or more input whitespace chars */
    if (iswspace((wint_t)*fmt)) {
      c = wsc_skipws(z);
      wsc_unget(z, c);
      continue;
    }
    /* ordinary character: must match the input exactly */
    if (*fmt != L'%') {
      c = wsc_get(z);
      if (c != (wint_t)*fmt) {
        if (c == WEOF)
          eof_hit = 1;
        wsc_unget(z, c);
        goto done;
      }
      continue;
    }
    fmt++;
    if (*fmt == L'%') {
      c = wsc_get(z);
      if (c != L'%') {
        if (c == WEOF)
          eof_hit = 1;
        wsc_unget(z, c);
        goto done;
      }
      continue;
    }

    int suppress = 0;
    if (*fmt == L'*') {
      suppress = 1;
      fmt++;
    }
    int width = 0, haswidth = 0;
    while (*fmt >= L'0' && *fmt <= L'9') {
      width = width * 10 + (int)(*fmt++ - L'0');
      haswidth = 1;
    }
    int lng = 0, shrt = 0;
    while (*fmt == L'l' || *fmt == L'h') {
      if (*fmt == L'l')
        lng++;
      else
        shrt++;
      fmt++;
    }
    wchar_t conv = *fmt;

    if (conv == L'd' || conv == L'i' || conv == L'u' || conv == L'o' ||
        conv == L'x' || conv == L'X' || conv == L'p') {
      int base = (conv == L'x' || conv == L'X' || conv == L'p') ? 16
                 : (conv == L'o')                               ? 8
                 : (conv == L'i')                               ? 0
                                                                : 10;
      int w = haswidth ? width : 63;
      if (w > 63)
        w = 63;
      int n = 0, sawdigit = 0, eff = base;
      c = wsc_skipws(z);
      if (c == WEOF) {
        eof_hit = 1;
        goto done;
      }
      if ((c == L'+' || c == L'-') && n < w) {
        tok[n++] = (char)c;
        c = wsc_get(z);
      }
      if (c == L'0' && n < w) {
        tok[n++] = '0';
        sawdigit = 1;
        c = wsc_get(z);
        if ((c == L'x' || c == L'X') && (base == 16 || base == 0) && n < w) {
          tok[n++] = (char)c;
          eff = 16;
          c = wsc_get(z);
        } else if (base == 0) {
          eff = 8; /* leading 0 -> octal for %i */
        }
      }
      if (eff == 0)
        eff = 10;
      while (c != WEOF && n < w && wdigit_ok(c, eff)) {
        tok[n++] = (char)c;
        sawdigit = 1;
        c = wsc_get(z);
      }
      wsc_unget(z, c);
      tok[n] = 0;
      if (!sawdigit)
        goto done; /* matching failure */
      if (!suppress) {
        int cbase = (conv == L'i') ? 0 : base;
        if (conv == L'd' || conv == L'i') {
          long long v = strtoll(tok, NULL, cbase);
          if (lng >= 2)
            *va_arg(ap, long long *) = v;
          else if (lng == 1)
            *va_arg(ap, long *) = (long)v;
          else if (shrt >= 2)
            *va_arg(ap, signed char *) = (signed char)v;
          else if (shrt == 1)
            *va_arg(ap, short *) = (short)v;
          else
            *va_arg(ap, int *) = (int)v;
        } else {
          unsigned long long uv = strtoull(tok, NULL, cbase);
          if (conv == L'p')
            *va_arg(ap, void **) = (void *)(unsigned long)uv;
          else if (lng >= 2)
            *va_arg(ap, unsigned long long *) = uv;
          else if (lng == 1)
            *va_arg(ap, unsigned long *) = (unsigned long)uv;
          else if (shrt >= 2)
            *va_arg(ap, unsigned char *) = (unsigned char)uv;
          else if (shrt == 1)
            *va_arg(ap, unsigned short *) = (unsigned short)uv;
          else
            *va_arg(ap, unsigned int *) = (unsigned int)uv;
        }
        count++;
      }
    } else if (conv == L'f' || conv == L'e' || conv == L'g' || conv == L'a' ||
               conv == L'F' || conv == L'E' || conv == L'G' || conv == L'A') {
      int w = haswidth ? width : 63;
      if (w > 63)
        w = 63;
      int n = 0, any = 0, ishex = 0;
      c = wsc_skipws(z);
      if (c == WEOF) {
        eof_hit = 1;
        goto done;
      }
      if ((c == L'+' || c == L'-') && n < w) {
        tok[n++] = (char)c;
        c = wsc_get(z);
      }
      /* A "0x"/"0X" prefix selects a hexadecimal float (hex mantissa, binary
         'p' exponent) -- the form printf("%a") emits. */
      if (c == L'0' && n < w) {
        wint_t c2 = wsc_get(z);
        if ((c2 == L'x' || c2 == L'X') && n + 1 < w) {
          ishex = 1;
          tok[n++] = '0';
          tok[n++] = (char)c2;
          c = wsc_get(z);
          while (wxdigit(c) && n < w) {
            tok[n++] = (char)c;
            any = 1;
            c = wsc_get(z);
          }
          if (c == L'.' && n < w) {
            tok[n++] = '.';
            c = wsc_get(z);
            while (wxdigit(c) && n < w) {
              tok[n++] = (char)c;
              any = 1;
              c = wsc_get(z);
            }
          }
          if (any && (c == L'p' || c == L'P') && n < w) {
            tok[n++] = (char)c;
            c = wsc_get(z);
            if ((c == L'+' || c == L'-') && n < w) {
              tok[n++] = (char)c;
              c = wsc_get(z);
            }
            while (c >= L'0' && c <= L'9' && n < w) {
              tok[n++] = (char)c;
              c = wsc_get(z);
            }
          }
        } else {
          wsc_unget(z, c2); /* a plain leading 0 in a decimal float */
        }
      }
      if (!ishex) {
        while (c >= L'0' && c <= L'9' && n < w) {
          tok[n++] = (char)c;
          any = 1;
          c = wsc_get(z);
        }
        if (c == L'.' && n < w) {
          tok[n++] = '.';
          c = wsc_get(z);
          while (c >= L'0' && c <= L'9' && n < w) {
            tok[n++] = (char)c;
            any = 1;
            c = wsc_get(z);
          }
        }
        if (any && (c == L'e' || c == L'E') && n < w) {
          tok[n++] = (char)c;
          c = wsc_get(z);
          if ((c == L'+' || c == L'-') && n < w) {
            tok[n++] = (char)c;
            c = wsc_get(z);
          }
          while (c >= L'0' && c <= L'9' && n < w) {
            tok[n++] = (char)c;
            c = wsc_get(z);
          }
        }
      }
      wsc_unget(z, c);
      tok[n] = 0;
      if (!any)
        goto done;
      if (!suppress) {
        double dv = ishex ? w_hexfloat(tok) : strtod(tok, NULL);
        if (lng)
          *va_arg(ap, double *) = dv;
        else
          *va_arg(ap, float *) = (float)dv;
        count++;
      }
    } else if (conv == L's') {
      int w = haswidth ? width : 0x7fffffff;
      c = wsc_skipws(z);
      if (c == WEOF) {
        eof_hit = 1;
        goto done;
      }
      mbstate_t st;
      memset(&st, 0, sizeof st);
      char *cout = NULL;
      wchar_t *wout = NULL;
      if (!suppress) {
        if (lng)
          wout = va_arg(ap, wchar_t *);
        else
          cout = va_arg(ap, char *);
      }
      int k = 0, got = 0;
      while (c != WEOF && !iswspace(c) && got < w) {
        if (lng) {
          if (wout)
            wout[k++] = (wchar_t)c;
        } else if (cout) {
          char mb[8];
          size_t r = wcrtomb(mb, (wchar_t)c, &st);
          if (r != (size_t)-1)
            for (size_t i = 0; i < r; i++)
              cout[k++] = mb[i];
        }
        got++;
        c = wsc_get(z);
      }
      wsc_unget(z, c);
      if (lng) {
        if (wout)
          wout[k] = 0;
      } else if (cout) {
        cout[k] = '\0';
      }
      if (!suppress)
        count++;
    } else if (conv == L'c') {
      int w = haswidth ? width : 1;
      mbstate_t st;
      memset(&st, 0, sizeof st);
      char *cout = NULL;
      wchar_t *wout = NULL;
      if (!suppress) {
        if (lng)
          wout = va_arg(ap, wchar_t *);
        else
          cout = va_arg(ap, char *);
      }
      int k = 0, got = 0;
      while (got < w) {
        c = wsc_get(z);
        if (c == WEOF)
          break;
        if (lng) {
          if (wout)
            wout[k++] = (wchar_t)c;
        } else if (cout) {
          char mb[8];
          size_t r = wcrtomb(mb, (wchar_t)c, &st);
          if (r != (size_t)-1)
            for (size_t i = 0; i < r; i++)
              cout[k++] = mb[i];
        }
        got++;
      }
      if (got < w) { /* fewer than width wide chars available */
        eof_hit = 1;
        goto done;
      }
      if (!suppress)
        count++;
      /* %c adds no terminator */
    } else if (conv == L'n') {
      if (!suppress) {
        if (lng >= 2)
          *va_arg(ap, long long *) = (long long)z->nread;
        else if (lng == 1)
          *va_arg(ap, long *) = (long)z->nread;
        else if (shrt >= 2)
          *va_arg(ap, signed char *) = (signed char)z->nread;
        else if (shrt == 1)
          *va_arg(ap, short *) = (short)z->nread;
        else
          *va_arg(ap, int *) = (int)z->nread;
      }
      /* %n is not counted as a conversion */
    } else if (conv == L'[') {
      /* scanset: read wide characters that are (or, after a leading '^', are
         not) members of the bracketed set.  Unlike %s it does NOT skip leading
         whitespace.  A ']' right after '[' or '[^' is a literal member. */
      const wchar_t *q = fmt + 1;
      int negate = 0;
      if (*q == L'^') {
        negate = 1;
        q++;
      }
      unsigned char inset[128];
      for (int i = 0; i < 128; i++)
        inset[i] = 0;
      wchar_t wset[WSET_WIDE];
      int nwset = 0;
      if (*q == L']') {
        inset[(unsigned char)']'] = 1;
        q++;
      }
      while (*q && *q != L']') {
        wchar_t m = *q++;
        if (m < 128)
          inset[m] = 1;
        else if (nwset < WSET_WIDE)
          wset[nwset++] = m;
      }
      fmt = (*q == L']') ? q : q - 1; /* leave fmt on ']' for the outer step */

      int w = haswidth ? width : 0x7fffffff;
      mbstate_t st;
      memset(&st, 0, sizeof st);
      char *cout = NULL;
      wchar_t *wout = NULL;
      if (!suppress) {
        if (lng)
          wout = va_arg(ap, wchar_t *);
        else
          cout = va_arg(ap, char *);
      }
      int k = 0, got = 0;
      c = wsc_get(z);
      while (c != WEOF && got < w) {
        int member;
        if (c < 128)
          member = inset[c] ? 1 : 0;
        else {
          member = 0;
          for (int i = 0; i < nwset; i++)
            if (wset[i] == (wchar_t)c) {
              member = 1;
              break;
            }
        }
        if (negate)
          member = !member;
        if (!member)
          break;
        if (lng) {
          if (wout)
            wout[k++] = (wchar_t)c;
        } else if (cout) {
          char mb[8];
          size_t r = wcrtomb(mb, (wchar_t)c, &st);
          if (r != (size_t)-1)
            for (size_t i = 0; i < r; i++)
              cout[k++] = mb[i];
        }
        got++;
        c = wsc_get(z);
      }
      wsc_unget(z, c);
      if (lng) {
        if (wout)
          wout[k] = 0;
      } else if (cout) {
        cout[k] = '\0';
      }
      if (got == 0) { /* matched nothing: EOF -> input failure, else mismatch */
        if (c == WEOF)
          eof_hit = 1;
        goto done;
      }
      if (!suppress)
        count++;
    } else {
      goto done; /* unknown conversion */
    }
  }

done:
  return (count == 0 && eof_hit) ? EOF : count;
}

int vswscanf(const wchar_t *s, const wchar_t *fmt, va_list ap) {
  _wscan z;
  z.fp = NULL;
  z.s = s;
  z.nread = 0;
  return _vwscan(&z, fmt, ap);
}

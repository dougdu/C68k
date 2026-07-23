#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include "libc_internal.h"

/* =====================================================================
 * Character-streaming scanf engine, shared by sscanf (string source) and
 * scanf/fscanf (stream source).  It reads one character at a time with a
 * single character of pushback (ungetc for streams, cursor rewind for
 * strings), so each conversion consumes exactly the characters it matches
 * and leaves everything after it -- including the terminating whitespace or
 * newline -- for the next read.
 *
 * Conversions: %d %i %u %o %x %X %p, %f %e %g %a (and %E %G %A %F, incl.
 * hexadecimal floats), %s, %[...] (scanset), %c, %n, %%, with field width,
 * '*' assignment suppression, and hh/h/l/ll length modifiers.  A malformed
 * numeric prefix -- a lone sign, "0x" with no digit, or "e"/"p" with no
 * exponent digit -- may consume that one extra character rather than leaving
 * it in the stream.
 * ===================================================================== */

/* Fetch the next input character (consumed), or EOF. */
static int sc_get(_scan *z) {
  int c;
  if (z->fp) {
    c = fgetc(z->fp);
  } else {
    unsigned char u = (unsigned char)*z->s;
    if (!u)
      return EOF;
    z->s++;
    c = u;
  }
  if (c != EOF)
    z->nread++;
  return c;
}

/* Push the last-read character back so the next sc_get returns it again. */
static void sc_unget(_scan *z, int c) {
  if (c == EOF)
    return;
  z->nread--;
  if (z->fp)
    ungetc(c, z->fp);
  else
    z->s--;
}

/* First non-whitespace character (consumed), or EOF. */
static int sc_skipws(_scan *z) {
  int c;
  do {
    c = sc_get(z);
  } while (c != EOF && isspace(c));
  return c;
}

/* Is c a valid digit in the given base (2..16)? */
static int digit_ok(int c, int base) {
  int d;
  if (c >= '0' && c <= '9')
    d = c - '0';
  else {
    int lc = c | 0x20;
    if (lc < 'a' || lc > 'z')
      return 0;
    d = lc - 'a' + 10;
  }
  return d < base;
}

extern double ldexp(double x, int n);

/* Convert a hex-float token "[+/-]0x h.hh [p[+/-]d]" to double without going
 * through strtod/atod: build the mantissa as a double (16 hex digits already
 * exceed a double's 53-bit precision, so accumulating rounds correctly) and
 * scale by a power of two with ldexp.  This is the inverse of printf("%a"). */
static double sc_hexfloat(const char *s) {
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

int _vscan(_scan *z, const char *fmt, va_list ap) {
  int count = 0;
  int eof_hit = 0;
  int c;
  char tok[64];

  for (; *fmt; fmt++) {
    /* whitespace directive: match zero or more input whitespace chars */
    if (isspace((unsigned char)*fmt)) {
      c = sc_skipws(z);
      sc_unget(z, c);
      continue;
    }
    /* ordinary character: must match the input exactly */
    if (*fmt != '%') {
      c = sc_get(z);
      if (c != (unsigned char)*fmt) {
        if (c == EOF)
          eof_hit = 1;
        sc_unget(z, c);
        goto done;
      }
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      c = sc_get(z);
      if (c != '%') {
        if (c == EOF)
          eof_hit = 1;
        sc_unget(z, c);
        goto done;
      }
      continue;
    }

    int suppress = 0;
    if (*fmt == '*') {
      suppress = 1;
      fmt++;
    }
    int width = 0, haswidth = 0;
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt++ - '0');
      haswidth = 1;
    }
    int lng = 0, shrt = 0;
    while (*fmt == 'l' || *fmt == 'h') {
      if (*fmt == 'l')
        lng++;
      else
        shrt++;
      fmt++;
    }
    char conv = *fmt;

    if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
        conv == 'x' || conv == 'X' || conv == 'p') {
      int base = (conv == 'x' || conv == 'X' || conv == 'p') ? 16
                 : (conv == 'o')                             ? 8
                 : (conv == 'i')                             ? 0
                                                             : 10;
      int w = haswidth ? width : 63;
      if (w > 63)
        w = 63;
      int n = 0, sawdigit = 0, eff = base;
      c = sc_skipws(z);
      if (c == EOF) {
        eof_hit = 1;
        goto done;
      }
      if ((c == '+' || c == '-') && n < w) {
        tok[n++] = (char)c;
        c = sc_get(z);
      }
      if (c == '0' && n < w) {
        tok[n++] = '0';
        sawdigit = 1;
        c = sc_get(z);
        if ((c == 'x' || c == 'X') && (base == 16 || base == 0) && n < w) {
          tok[n++] = (char)c;
          eff = 16;
          c = sc_get(z);
        } else if (base == 0) {
          eff = 8; /* leading 0 -> octal for %i */
        }
      }
      if (eff == 0)
        eff = 10;
      while (c != EOF && n < w && digit_ok(c, eff)) {
        tok[n++] = (char)c;
        sawdigit = 1;
        c = sc_get(z);
      }
      sc_unget(z, c);
      tok[n] = 0;
      if (!sawdigit)
        goto done; /* matching failure */
      if (!suppress) {
        int cbase = (conv == 'i') ? 0 : base;
        if (conv == 'd' || conv == 'i') {
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
          if (conv == 'p')
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
    } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'a' ||
               conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A') {
      int w = haswidth ? width : 63;
      if (w > 63)
        w = 63;
      int n = 0, any = 0, ishex = 0;
      c = sc_skipws(z);
      if (c == EOF) {
        eof_hit = 1;
        goto done;
      }
      if ((c == '+' || c == '-') && n < w) {
        tok[n++] = (char)c;
        c = sc_get(z);
      }
      /* A "0x"/"0X" prefix selects a hexadecimal float (hex mantissa, binary
         'p' exponent) -- the form printf("%a") emits. */
      if (c == '0' && n < w) {
        int c2 = sc_get(z);
        if ((c2 == 'x' || c2 == 'X') && n + 1 < w) {
          ishex = 1;
          tok[n++] = '0';
          tok[n++] = (char)c2;
          c = sc_get(z);
          while (isxdigit(c) && n < w) {
            tok[n++] = (char)c;
            any = 1;
            c = sc_get(z);
          }
          if (c == '.' && n < w) {
            tok[n++] = '.';
            c = sc_get(z);
            while (isxdigit(c) && n < w) {
              tok[n++] = (char)c;
              any = 1;
              c = sc_get(z);
            }
          }
          if (any && (c == 'p' || c == 'P') && n < w) {
            tok[n++] = (char)c;
            c = sc_get(z);
            if ((c == '+' || c == '-') && n < w) {
              tok[n++] = (char)c;
              c = sc_get(z);
            }
            while (c >= '0' && c <= '9' && n < w) {
              tok[n++] = (char)c;
              c = sc_get(z);
            }
          }
        } else {
          sc_unget(z, c2); /* a plain leading 0 in a decimal float */
        }
      }
      if (!ishex) {
        while (c >= '0' && c <= '9' && n < w) {
          tok[n++] = (char)c;
          any = 1;
          c = sc_get(z);
        }
        if (c == '.' && n < w) {
          tok[n++] = '.';
          c = sc_get(z);
          while (c >= '0' && c <= '9' && n < w) {
            tok[n++] = (char)c;
            any = 1;
            c = sc_get(z);
          }
        }
        if (any && (c == 'e' || c == 'E') && n < w) {
          tok[n++] = (char)c;
          c = sc_get(z);
          if ((c == '+' || c == '-') && n < w) {
            tok[n++] = (char)c;
            c = sc_get(z);
          }
          while (c >= '0' && c <= '9' && n < w) {
            tok[n++] = (char)c;
            c = sc_get(z);
          }
        }
      }
      sc_unget(z, c);
      tok[n] = 0;
      if (!any)
        goto done;
      if (!suppress) {
        double dv = ishex ? sc_hexfloat(tok) : strtod(tok, NULL);
        if (lng)
          *va_arg(ap, double *) = dv;
        else
          *va_arg(ap, float *) = (float)dv;
        count++;
      }
    } else if (conv == 's') {
      int w = haswidth ? width : 0x7fffffff;
      c = sc_skipws(z);
      if (c == EOF) {
        eof_hit = 1;
        goto done;
      }
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      while (c != EOF && !isspace(c) && k < w) {
        if (out)
          out[k] = (char)c;
        k++;
        c = sc_get(z);
      }
      sc_unget(z, c);
      if (out)
        out[k] = '\0';
      if (!suppress)
        count++;
    } else if (conv == 'c') {
      int w = haswidth ? width : 1;
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      while (k < w) {
        c = sc_get(z);
        if (c == EOF)
          break;
        if (out)
          out[k] = (char)c;
        k++;
      }
      if (k < w) { /* fewer than width chars available */
        eof_hit = 1;
        goto done;
      }
      if (!suppress)
        count++;
    } else if (conv == 'n') {
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
    } else if (conv == '[') {
      /* scanset: read characters that are (or, after a leading '^', are not)
         members of the bracketed set.  Unlike %s it does NOT skip leading
         whitespace.  A ']' right after '[' or '[^' is a literal member. */
      const char *q = fmt + 1;
      int negate = 0;
      if (*q == '^') {
        negate = 1;
        q++;
      }
      unsigned char inset[256];
      for (int i = 0; i < 256; i++)
        inset[i] = 0;
      if (*q == ']') {
        inset[(unsigned char)']'] = 1;
        q++;
      }
      while (*q && *q != ']') {
        inset[(unsigned char)*q] = 1;
        q++;
      }
      fmt = (*q == ']') ? q : q - 1; /* leave fmt on ']' for the outer step */

      int w = haswidth ? width : 0x7fffffff;
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      c = sc_get(z);
      while (c != EOF && k < w) {
        int member = inset[(unsigned char)c] ? 1 : 0;
        if (negate)
          member = !member;
        if (!member)
          break;
        if (out)
          out[k] = (char)c;
        k++;
        c = sc_get(z);
      }
      sc_unget(z, c);
      if (out)
        out[k] = '\0';
      if (k == 0) { /* matched nothing: EOF -> input failure, else mismatch */
        if (c == EOF)
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

int vsscanf(const char *s, const char *fmt, va_list ap) {
  _scan z;
  z.fp = NULL;
  z.s = s;
  z.nread = 0;
  return _vscan(&z, fmt, ap);
}

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>

/* =====================================================================
 * sscanf -- formatted input from a NUL-terminated string.
 * Supports %d %i %u %o %x %c %s %f/%e/%g %%, field width, '*' suppress,
 * and length modifiers h / l / ll.
 * ===================================================================== */
int vsscanf(const char *s, const char *fmt, va_list ap) {
  int count = 0;
  for (; *fmt; fmt++) {
    if (isspace((unsigned char)*fmt)) {
      while (isspace((unsigned char)*s))
        s++;
      continue;
    }
    if (*fmt != '%') {
      if (*s != *fmt)
        break;
      s++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (*s != '%')
        break;
      s++;
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
      while (isspace((unsigned char)*s))
        s++;
      int base = (conv == 'x' || conv == 'X' || conv == 'p')
                     ? 16
                     : (conv == 'o' ? 8 : 10);
      char *end;
      int is_signed = (conv == 'd' || conv == 'i');
      unsigned long uv;
      long sv;
      if (is_signed) {
        sv = strtol(s, &end, base);
        uv = (unsigned long)sv;
      } else {
        uv = strtoul(s, &end, base);
        sv = (long)uv;
      }
      if (end == s)
        break;
      s = end;
      if (!suppress) {
        if (lng >= 2)
          *va_arg(ap, long long *) = (long long)sv;
        else if (lng == 1)
          *va_arg(ap, long *) = sv;
        else if (shrt)
          *va_arg(ap, short *) = (short)sv;
        else
          *va_arg(ap, int *) = (int)sv;
        count++;
      }
    } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' ||
               conv == 'G') {
      while (isspace((unsigned char)*s))
        s++;
      char *end;
      double dv = strtod(s, &end);
      if (end == s)
        break;
      s = end;
      if (!suppress) {
        if (lng)
          *va_arg(ap, double *) = dv;
        else
          *va_arg(ap, float *) = (float)dv;
        count++;
      }
    } else if (conv == 's') {
      while (isspace((unsigned char)*s))
        s++;
      if (!*s)
        break;
      int w = haswidth ? width : 0x7fffffff;
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      while (*s && !isspace((unsigned char)*s) && k < w) {
        if (out)
          out[k] = *s;
        k++;
        s++;
      }
      if (out)
        out[k] = '\0';
      if (!suppress)
        count++;
    } else if (conv == 'c') {
      int w = haswidth ? width : 1;
      char *out = suppress ? NULL : va_arg(ap, char *);
      int k = 0;
      while (*s && k < w) {
        if (out)
          out[k] = *s;
        k++;
        s++;
      }
      if (k < w)
        break;
      if (!suppress)
        count++;
    } else {
      break;
    }
  }
  return count;
}

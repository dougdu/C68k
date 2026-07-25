#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "libc_internal.h"

/* Wide stream scanf: the shared wide scanner (vwscanf.c) reads straight from
 * the FILE via fgetwc/ungetwc, so conversions consume exactly what they match
 * and leave the rest in the stream for the next call. */
int vfwscanf(FILE *fp, const wchar_t *fmt, va_list ap) {
  _wscan z;
  z.fp = fp;
  z.s = NULL;
  z.nread = 0;
  return _vwscan(&z, fmt, ap);
}

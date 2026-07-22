#include <stdio.h>
#include <stdarg.h>
#include "libc_internal.h"

/* Stream scanf: the shared character-streaming engine (vsscanf.c) reads
 * straight from the FILE via fgetc/ungetc, so conversions consume exactly
 * what they match and leave the rest in the stream for the next call. */
int vfscanf(FILE *fp, const char *fmt, va_list ap) {
  _scan z;
  z.fp = fp;
  z.s = NULL;
  z.nread = 0;
  return _vscan(&z, fmt, ap);
}

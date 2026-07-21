#include <stdio.h>
#include <stdarg.h>
#include "libc_internal.h"

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
  _psink s;
  s.fp = fp;
  s.buf = NULL;
  s.cap = 0;
  s.len = 0;
  return _vformat(&s, fmt, ap);
}

#include <stdio.h>
#include <stdarg.h>
#include "libc_internal.h"

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  _psink s;
  s.fp = NULL;
  s.buf = buf;
  s.cap = (int)size;
  s.len = 0;
  _vformat(&s, fmt, ap);
  if (size > 0)
    buf[s.len < (int)size ? s.len : (int)size - 1] = 0;
  return s.len;
}

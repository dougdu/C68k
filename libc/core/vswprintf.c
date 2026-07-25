#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>
#include "libc_internal.h"

/* Unlike vsnprintf, C99 swprintf returns a negative value when the output was
   truncated (n or more wide characters would be produced). */
int vswprintf(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap) {
  _pwsink s;
  s.fp = NULL;
  s.buf = buf;
  s.cap = (int)size;
  s.len = 0;
  _vwformat(&s, fmt, ap);
  if (size > 0)
    buf[s.len < (int)size ? s.len : (int)size - 1] = 0;
  return (s.len < (int)size) ? s.len : -1;
}

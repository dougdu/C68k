#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>
#include "libc_internal.h"

int vfwprintf(FILE *fp, const wchar_t *fmt, va_list ap) {
  _pwsink s;
  s.fp = fp;
  s.buf = NULL;
  s.cap = 0;
  s.len = 0;
  return _vwformat(&s, fmt, ap);
}

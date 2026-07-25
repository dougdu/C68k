#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

int swprintf(wchar_t *buf, size_t size, const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vswprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

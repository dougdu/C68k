#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

int swscanf(const wchar_t *s, const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vswscanf(s, fmt, ap);
  va_end(ap);
  return n;
}

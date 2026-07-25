#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

int wprintf(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vwprintf(fmt, ap);
  va_end(ap);
  return n;
}

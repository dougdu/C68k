#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

int wscanf(const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfwscanf(stdin, fmt, ap);
  va_end(ap);
  return n;
}

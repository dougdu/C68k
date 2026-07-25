#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

int fwprintf(FILE *fp, const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfwprintf(fp, fmt, ap);
  va_end(ap);
  return n;
}

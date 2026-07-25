#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

int fwscanf(FILE *fp, const wchar_t *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfwscanf(fp, fmt, ap);
  va_end(ap);
  return n;
}

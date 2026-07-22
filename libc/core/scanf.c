#include <stdio.h>
#include <stdarg.h>

int scanf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfscanf(stdin, fmt, ap);
  va_end(ap);
  return n;
}

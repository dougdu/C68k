#include <stdio.h>
#include <stdarg.h>

int sscanf(const char *s, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsscanf(s, fmt, ap);
  va_end(ap);
  return n;
}

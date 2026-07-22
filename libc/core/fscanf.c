#include <stdio.h>
#include <stdarg.h>

int fscanf(FILE *fp, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vfscanf(fp, fmt, ap);
  va_end(ap);
  return n;
}

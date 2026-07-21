#include <stdio.h>
#include <stdlib.h>

void __assert_fail(const char *expr, const char *file, int line) {
  fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line);
  abort();
}

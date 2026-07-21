#include <stdlib.h>
#include <string.h>

char *strdup(const char *s) {
  size_t n = strlen(s);
  char *p = malloc(n + 1);
  if (p)
    memcpy(p, s, n + 1);
  return p;
}

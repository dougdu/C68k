#include <stdlib.h>
#include <string.h>

char *strndup(const char *s, size_t n) {
  size_t len = 0;
  while (len < n && s[len])
    len++;
  char *p = malloc(len + 1);
  if (p) {
    memcpy(p, s, len);
    p[len] = 0;
  }
  return p;
}

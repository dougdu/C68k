#include <string.h>

char *strncat(char *dst, const char *src, size_t n) {
  char *d = dst + strlen(dst);
  size_t i = 0;
  for (; i < n && src[i]; i++)
    d[i] = src[i];
  d[i] = 0;
  return dst;
}

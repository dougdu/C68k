#include <string.h>

/* Collation on these targets uses the "C" locale's byte order for every
 * locale (no locale-specific collating sequence is wired), so strcoll is a
 * plain byte comparison and strxfrm is an identity copy whose strcmp order
 * matches strcoll. */

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

size_t strxfrm(char *dst, const char *src, size_t n) {
  size_t len = strlen(src);
  if (n) {
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++)
      dst[i] = src[i];
    dst[i] = 0;
  }
  return len;
}

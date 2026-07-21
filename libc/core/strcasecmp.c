#include <strings.h>

/* Case-insensitive compares share the lowercase fold, so they stay in one
 * cohesive object. */
static int _lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strncasecmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int ca = _lc((unsigned char)a[i]), cb = _lc((unsigned char)b[i]);
    if (ca != cb)
      return ca - cb;
    if (!ca)
      return 0;
  }
  return 0;
}

int strcasecmp(const char *a, const char *b) {
  for (;; a++, b++) {
    int ca = _lc((unsigned char)*a), cb = _lc((unsigned char)*b);
    if (ca != cb)
      return ca - cb;
    if (!ca)
      return 0;
  }
}

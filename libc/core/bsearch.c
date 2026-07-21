#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *)) {
  const char *a = (const char *)base;
  size_t lo = 0, hi = nmemb;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = cmp(key, a + mid * size);
    if (c < 0)
      hi = mid;
    else if (c > 0)
      lo = mid + 1;
    else
      return (void *)(a + mid * size);
  }
  return NULL;
}

#include <string.h>

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = a, *pb = b;
  for (; n; n--, pa++, pb++)
    if (*pa != *pb)
      return (int)*pa - (int)*pb;
  return 0;
}

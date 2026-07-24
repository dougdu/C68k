#include <string.h>

void *memmem(const void *hay, size_t haylen, const void *needle, size_t nlen) {
  if (nlen == 0)
    return (void *)hay;
  if (haylen < nlen)
    return 0;
  const char *h = (const char *)hay;
  for (size_t i = 0; i + nlen <= haylen; i++)
    if (h[i] == *(const char *)needle && memcmp(h + i, needle, nlen) == 0)
      return (void *)(h + i);
  return 0;
}

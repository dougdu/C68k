#include <string.h>

size_t strlcpy(char *dst, const char *src, size_t size) {
  size_t srclen = strlen(src);
  if (size) {
    size_t n = (srclen < size - 1) ? srclen : size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}

size_t strlcat(char *dst, const char *src, size_t size) {
  size_t dlen = 0;
  while (dlen < size && dst[dlen])
    dlen++;
  size_t srclen = strlen(src);
  if (dlen == size)
    return size + srclen; /* dst was not NUL-terminated within size */
  size_t n = (srclen < size - dlen - 1) ? srclen : size - dlen - 1;
  memcpy(dst + dlen, src, n);
  dst[dlen + n] = '\0';
  return dlen + srclen;
}

#include <stdio.h>

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
  const unsigned char *p = ptr;
  size_t total = size * nmemb;
  for (size_t i = 0; i < total; i++)
    if (fputc(p[i], fp) == EOF)
      return size ? i / size : 0;
  return nmemb;
}

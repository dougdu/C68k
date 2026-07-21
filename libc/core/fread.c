#include <stdio.h>

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
  unsigned char *p = ptr;
  size_t total = size * nmemb;
  for (size_t i = 0; i < total; i++) {
    int c = fgetc(fp);
    if (c == EOF)
      return size ? i / size : 0;
    p[i] = (unsigned char)c;
  }
  return nmemb;
}

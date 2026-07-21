#include <stdio.h>

int fputs(const char *s, FILE *fp) {
  while (*s)
    if (fputc((unsigned char)*s++, fp) == EOF)
      return EOF;
  return 0;
}

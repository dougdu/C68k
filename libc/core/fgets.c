#include <stdio.h>

char *fgets(char *s, int n, FILE *fp) {
  int i = 0;
  while (i < n - 1) {
    int c = fgetc(fp);
    if (c == EOF)
      break;
    s[i++] = (char)c;
    if (c == '\n')
      break;
  }
  if (i == 0)
    return NULL;
  s[i] = 0;
  return s;
}

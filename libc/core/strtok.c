#include <string.h>

char *strtok(char *s, const char *delim) {
  static char *save;
  if (!s)
    s = save;
  if (!s)
    return NULL;
  while (*s && strchr(delim, (unsigned char)*s))
    s++;
  if (!*s) {
    save = NULL;
    return NULL;
  }
  char *tok = s;
  while (*s && !strchr(delim, (unsigned char)*s))
    s++;
  if (*s) {
    *s = 0;
    save = s + 1;
  } else {
    save = NULL;
  }
  return tok;
}

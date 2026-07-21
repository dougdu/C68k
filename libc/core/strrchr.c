#include <string.h>

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  for (; *s; s++)
    if (*s == (char)c)
      last = s;
  if (c == 0)
    return (char *)s;
  return (char *)last;
}

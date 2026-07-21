#include <string.h>

char *strstr(const char *hay, const char *needle) {
  size_t nl = strlen(needle);
  if (nl == 0)
    return (char *)hay;
  for (; *hay; hay++)
    if (strncmp(hay, needle, nl) == 0)
      return (char *)hay;
  return NULL;
}

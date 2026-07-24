#include <strings.h>
#include <ctype.h>

char *strcasestr(const char *hay, const char *needle) {
  if (!*needle)
    return (char *)hay;
  for (; *hay; hay++) {
    const char *h = hay, *n = needle;
    while (*h && *n &&
           tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
      h++;
      n++;
    }
    if (!*n)
      return (char *)hay;
  }
  return 0;
}

#include <string.h>

char *strpbrk(const char *s, const char *accept) {
  for (; *s; s++) {
    const char *a;
    for (a = accept; *a; a++)
      if (*a == *s)
        return (char *)s;
  }
  return NULL;
}

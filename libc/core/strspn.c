#include <string.h>

size_t strspn(const char *s, const char *accept) {
  const char *p;
  for (p = s; *p; p++) {
    const char *a;
    for (a = accept; *a && *a != *p; a++)
      ;
    if (!*a)
      break; /* *p is not in `accept` -> span ends */
  }
  return (size_t)(p - s);
}

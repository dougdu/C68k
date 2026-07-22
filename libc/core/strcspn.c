#include <string.h>

size_t strcspn(const char *s, const char *reject) {
  const char *p;
  for (p = s; *p; p++) {
    const char *r;
    for (r = reject; *r && *r != *p; r++)
      ;
    if (*r)
      break; /* *p is in `reject` -> span ends */
  }
  return (size_t)(p - s);
}

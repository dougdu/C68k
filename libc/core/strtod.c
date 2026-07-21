#include <stdlib.h>
#include <ctype.h>
#include "libc_internal.h"

double strtod(const char *s, char **end) {
  const char *p = s;
  while (isspace((unsigned char)*p))
    p++;
  const char *start = p;
  if (*p == '+' || *p == '-')
    p++;
  while (isdigit((unsigned char)*p))
    p++;
  if (*p == '.') {
    p++;
    while (isdigit((unsigned char)*p))
      p++;
  }
  if (*p == 'e' || *p == 'E') {
    const char *e = p + 1;
    if (*e == '+' || *e == '-')
      e++;
    if (isdigit((unsigned char)*e)) {
      p = e;
      while (isdigit((unsigned char)*p))
        p++;
    }
  }
  if (end)
    *end = (char *)p;
  return atod(start);
}

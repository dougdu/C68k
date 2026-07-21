#include <stdlib.h>

long long strtoll(const char *s, char **end, int base) {
  return (long long)strtoull(s, end, base);
}

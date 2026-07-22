#include <inttypes.h>
#include <stdlib.h>

uintmax_t strtoumax(const char *s, char **end, int base) {
  return (uintmax_t)strtoull(s, end, base);
}

#include <inttypes.h>
#include <stdlib.h>

intmax_t strtoimax(const char *s, char **end, int base) {
  return (intmax_t)strtoll(s, end, base);
}

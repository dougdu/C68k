#include <stdlib.h>

lldiv_t lldiv(long long num, long long den) {
  lldiv_t r;
  r.quot = num / den;
  r.rem = num % den;
  return r;
}

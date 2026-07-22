#include <inttypes.h>

imaxdiv_t imaxdiv(intmax_t num, intmax_t den) {
  imaxdiv_t r;
  r.quot = num / den;
  r.rem = num % den;
  return r;
}

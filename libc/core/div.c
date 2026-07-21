#include <stdlib.h>

div_t div(int num, int den) {
  div_t r;
  r.quot = num / den;
  r.rem = num % den;
  return r;
}

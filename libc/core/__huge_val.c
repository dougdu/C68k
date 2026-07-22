#include "math_priv.h"

double __huge_val(void) {
  __dbl b;
  b.u = 0x7FF0000000000000ULL; /* +infinity */
  return b.d;
}

#include "math_priv.h"

double __nan_val(void) {
  __dbl b;
  b.u = 0x7FF8000000000000ULL; /* quiet NaN */
  return b.d;
}

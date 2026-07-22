#include "math_priv.h"

double logb(double x) {
  __dbl b;
  b.d = x;
  int e = __DEXP(b.u);
  if (e == 0x7FF)
    return x * x; /* +inf for inf, nan for nan */
  if (x == 0.0)
    return -__huge_val(); /* logb(0) = -inf */
  if (e == 0) {           /* subnormal */
    b.d = x * 0x1p54;
    e = __DEXP(b.u) - 54;
  }
  return (double)(e - 1023);
}

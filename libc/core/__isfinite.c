#include "math_priv.h"

int __isfinite(double x) {
  __dbl b;
  b.d = x;
  return __DEXP(b.u) != 0x7FF;
}

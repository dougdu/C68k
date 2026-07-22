#include "math_priv.h"

int __signbit(double x) {
  __dbl b;
  b.d = x;
  return __DSIGN(b.u);
}

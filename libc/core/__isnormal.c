#include "math_priv.h"

int __isnormal(double x) {
  __dbl b;
  b.d = x;
  int e = __DEXP(b.u);
  return e != 0 && e != 0x7FF;
}

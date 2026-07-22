#include "math_priv.h"

int __fpclassify(double x) {
  __dbl b;
  b.d = x;
  int e = __DEXP(b.u);
  unsigned long long m = __DMANT(b.u);
  if (e == 0)
    return m ? FP_SUBNORMAL : FP_ZERO;
  if (e == 0x7FF)
    return m ? FP_NAN : FP_INFINITE;
  return FP_NORMAL;
}

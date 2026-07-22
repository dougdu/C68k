#include "math_priv.h"

int ilogb(double x) {
  __dbl b;
  b.d = x;
  int e = __DEXP(b.u);
  unsigned long long m = __DMANT(b.u);
  if (e == 0 && m == 0)
    return FP_ILOGB0;
  if (e == 0x7FF)
    return m ? FP_ILOGBNAN : 2147483647;
  if (e == 0) { /* subnormal */
    b.d = x * 0x1p54;
    e = __DEXP(b.u) - 54;
  }
  return e - 1023;
}

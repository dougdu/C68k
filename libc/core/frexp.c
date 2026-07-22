#include "math_priv.h"

/* x = m * 2^*e with 0.5 <= |m| < 1 (or x itself for 0/inf/nan). */
double frexp(double x, int *e) {
  __dbl b;
  b.d = x;
  int ee = __DEXP(b.u);
  if (ee == 0x7FF || x == 0.0) {
    *e = 0;
    return x;
  }
  if (ee == 0) { /* subnormal: scale into the normal range first */
    b.d = x * 0x1p54;
    ee = __DEXP(b.u) - 54;
  }
  *e = ee - 1022;
  b.u = (b.u & ~(0x7FFULL << 52)) | (1022ULL << 52); /* force exp -> [0.5,1) */
  return b.d;
}

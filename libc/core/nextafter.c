#include "math_priv.h"

double nextafter(double x, double y) {
  if (__isnan(x) || __isnan(y))
    return x + y;
  if (x == y)
    return y;
  __dbl b;
  b.d = x;
  if (x == 0.0) { /* step to the smallest subnormal toward y */
    __dbl r;
    r.u = 1;
    if (__signbit(y))
      r.u |= (1ULL << 63);
    return r.d;
  }
  /* IEEE bits are sign-magnitude: for x>0 a larger value has a larger u, for
     x<0 a larger value has a smaller u. */
  if (y > x) {
    if (__signbit(x))
      b.u--;
    else
      b.u++;
  } else {
    if (__signbit(x))
      b.u++;
    else
      b.u--;
  }
  return b.d;
}

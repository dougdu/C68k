#include "math_priv.h"

/* Cube root via pow(|x|, 1/3) with one Newton refinement (y = (2y + a/y^2)/3)
   to absorb the rounding of the 1/3 exponent. */
double cbrt(double x) {
  if (x == 0.0)
    return x;
  double a = __signbit(x) ? -x : x;
  double r;
  if (a < 1.0)
    r = 1.0 / powd(1.0 / a, 1.0 / 3.0); /* avoid powd result<1 (libm exp bug) */
  else
    r = powd(a, 1.0 / 3.0);
  r = (2.0 * r + a / (r * r)) / 3.0;
  return __signbit(x) ? -r : r;
}

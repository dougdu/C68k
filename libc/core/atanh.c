#include <errno.h>
#include "math_priv.h"

/* atanh(x) = 0.5 * log((1+x)/(1-x)) = 0.5 * log1p(2x/(1-x)), for |x| < 1. */
double atanh(double x) {
  double a = __signbit(x) ? -x : x;
  if (a > 1.0) {
    errno = EDOM;
    return __nan_val();
  }
  if (a == 1.0) {
    errno = ERANGE;
    return __signbit(x) ? -__huge_val() : __huge_val();
  }
  if (a < 1e-8)
    return x;
  double r = 0.5 * log1p(2.0 * a / (1.0 - a));
  return __signbit(x) ? -r : r;
}

#include "math_priv.h"

/* asinh(x) = log(x + sqrt(x^2 + 1)); use |x| to avoid cancellation for x<0. */
double asinh(double x) {
  double a = __signbit(x) ? -x : x;
  double r;
  if (a < 1e-7)
    return x; /* asinh(x) ~ x */
  if (a > 1e8)
    r = logd(a) + 0.69314718055994530942; /* log(2a) */
  else
    r = logd(a + sqrtd(a * a + 1.0));
  return __signbit(x) ? -r : r;
}

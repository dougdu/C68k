#include "math_priv.h"

/* sinh(x) = (e^x - e^-x)/2.  For small |x| the direct form cancels, so use
   expm1: with u = e^|x| - 1, sinh(|x|) = u(u+2) / (2(u+1)). */
double sinh(double x) {
  double a = __signbit(x) ? -x : x;
  double s;
  if (a < 1.0) {
    double u = expm1(a);
    s = u * (u + 2.0) / (2.0 * (u + 1.0));
  } else {
    double e = expd(a);
    s = (e - 1.0 / e) * 0.5;
  }
  return __signbit(x) ? -s : s;
}

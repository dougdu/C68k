#include "math_priv.h"

/* tanh(x) = (e^2|x| - 1)/(e^2|x| + 1) = u/(u+2) with u = expm1(2|x|). */
double tanh(double x) {
  double a = __signbit(x) ? -x : x;
  double t;
  if (a > 20.0)
    t = 1.0; /* saturated to within double precision */
  else {
    double u = expm1(2.0 * a);
    t = u / (u + 2.0);
  }
  return __signbit(x) ? -t : t;
}

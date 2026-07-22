#include "math_priv.h"

/* hypot(x,y) = sqrt(x^2 + y^2), scaled by the larger magnitude to avoid
   intermediate overflow/underflow. */
double hypot(double x, double y) {
  if (__isinf(x) || __isinf(y))
    return __huge_val(); /* hypot(inf, nan) is +inf */
  double a = __signbit(x) ? -x : x;
  double b = __signbit(y) ? -y : y;
  if (a < b) {
    double t = a;
    a = b;
    b = t;
  }
  if (a == 0.0)
    return 0.0;
  double r = b / a;
  return a * sqrtd(1.0 + r * r);
}

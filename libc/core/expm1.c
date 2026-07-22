#include "math_priv.h"

/* e^x - 1, conditioned so small x keeps full precision (Kahan's method:
   correct the exp(x)-1 cancellation by scaling with x/log(exp(x))). */
double expm1(double x) {
  double u = expd(x);
  if (u == 1.0)
    return x; /* x tiny */
  double um1 = u - 1.0;
  if (um1 == -1.0)
    return -1.0; /* x very negative -> e^x underflows to 0 */
  return um1 * (x / logd(u));
}

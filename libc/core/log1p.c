#include <errno.h>
#include "math_priv.h"

/* log(1 + x), conditioned for small x (Kahan's method). */
double log1p(double x) {
  if (x < -1.0) {
    errno = EDOM;
    return __nan_val();
  }
  if (x == -1.0) {
    errno = ERANGE;
    return -__huge_val();
  }
  double u = 1.0 + x;
  if (u == 1.0)
    return x; /* x tiny */
  return logd(u) * (x / (u - 1.0));
}

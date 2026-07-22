#include <errno.h>
#include "math_priv.h"

/* IEEE remainder: x - n*y where n = round-to-nearest-even of x/y. */
double remainder(double x, double y) {
  if (y == 0.0 || __isinf(x) || __isnan(x) || __isnan(y)) {
    if (__isnan(x) || __isnan(y))
      return __nan_val();
    errno = EDOM;
    return __nan_val();
  }
  if (__isinf(y))
    return x;
  double q = rint(x / y);
  return x - q * y;
}

#include <errno.h>
#include "math_priv.h"

/* Like remainder(), and *quo receives the low 3 bits of the quotient with the
   sign of x/y. */
double remquo(double x, double y, int *quo) {
  *quo = 0;
  if (y == 0.0 || __isinf(x) || __isnan(x) || __isnan(y)) {
    if (!(__isnan(x) || __isnan(y)))
      errno = EDOM;
    return __nan_val();
  }
  if (__isinf(y))
    return x;
  double qd = rint(x / y);
  double r = x - qd * y;
  *quo = (int)((long)qd % 8);
  return r;
}

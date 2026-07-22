#include <errno.h>
#include "math_priv.h"

/* acosh(x) = log(x + sqrt(x^2 - 1)), defined for x >= 1. */
double acosh(double x) {
  if (x < 1.0) {
    errno = EDOM;
    return __nan_val();
  }
  if (x > 1e8)
    return logd(x) + 0.69314718055994530942; /* log(2x) */
  return logd(x + sqrtd(x * x - 1.0));
}

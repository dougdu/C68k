#include "math_priv.h"

double fdim(double x, double y) {
  if (__isnan(x) || __isnan(y))
    return __nan_val();
  return x > y ? x - y : 0.0;
}

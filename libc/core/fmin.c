#include "math_priv.h"

double fmin(double x, double y) {
  if (__isnan(x))
    return y;
  if (__isnan(y))
    return x;
  return x < y ? x : y;
}

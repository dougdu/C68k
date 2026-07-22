#include "math_priv.h"

/* cosh(x) = (e^|x| + e^-|x|)/2; the two terms add, so no cancellation. */
double cosh(double x) {
  double e = expd(__signbit(x) ? -x : x);
  return (e + 1.0 / e) * 0.5;
}

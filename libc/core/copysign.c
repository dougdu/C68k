#include "math_priv.h"

double copysign(double x, double y) {
  __dbl bx, by;
  bx.d = x;
  by.d = y;
  bx.u = (bx.u & ~(1ULL << 63)) | (by.u & (1ULL << 63));
  return bx.d;
}

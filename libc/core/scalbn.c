#include "math_priv.h"

double scalbn(double x, int n) {
  __dbl b;
  double r = x;
  while (n > 1023) { /* chunk large exponents to avoid overflow in one 2^n */
    r *= 0x1p1023;
    n -= 1023;
  }
  while (n < -1022) {
    r *= 0x1p-1022;
    n += 1022;
  }
  b.u = ((unsigned long long)(n + 1023)) << 52; /* 2^n, n in [-1022,1023] */
  return r * b.d;
}

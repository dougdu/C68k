#include "math_priv.h"

/* Round to nearest integer, ties to even.  Uses floor + explicit tie handling
   rather than the (x + 2^52) - 2^52 trick: this form is exact and self-evidently
   ties-to-even, needing no assumption about the soft-float adder's rounding mode
   (which since the libm RNE fix would in fact make the trick viable too). */
double rint(double x) {
  __dbl b;
  b.d = x;
  if (__DEXP(b.u) >= 1023 + 52) /* already integral, or inf/nan */
    return x;
  double f = floord(x); /* floor toward -inf, so d is always in [0,1) */
  double d = x - f;
  if (d < 0.5)
    return f;
  if (d > 0.5)
    return f + 1.0;
  double h = f * 0.5; /* exact tie: round to even */
  return (floord(h) == h) ? f : f + 1.0;
}

#include "math_priv.h"

/* No fused path on this soft-float target: this rounds twice (x*y then +z).
   Adequate for portability; not a true single-rounding FMA. */
double fma(double x, double y, double z) { return x * y + z; }

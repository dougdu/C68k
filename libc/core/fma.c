#include "math_priv.h"

/* Correctly-rounded, single-rounding fused multiply-add from the libm IEEE-754
   runtime (core/dpfma.a68).  c68k prefixes C symbols with '_', so the C name
   `fmad` binds to the library's `_fmad`. */
extern double fmad(double, double, double);
double fma(double x, double y, double z) { return fmad(x, y, z); }

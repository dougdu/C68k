#include "math_priv.h"

/* long is 32-bit here, so it always fits int. */
double scalbln(double x, long n) { return scalbn(x, (int)n); }

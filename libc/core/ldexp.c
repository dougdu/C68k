#include "math_priv.h"

double ldexp(double x, int n) { return scalbn(x, n); }

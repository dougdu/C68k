#include "math_priv.h"

/* Round toward zero. */
double trunc(double x) { return x < 0.0 ? ceild(x) : floord(x); }

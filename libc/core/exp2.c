#include "math_priv.h"

/* 2^x = e^(x * ln2). */
double exp2(double x) { return __mexp(x * 0.69314718055994530942); }

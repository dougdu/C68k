#include "math_priv.h"

/*
 * Work around a vendored-libm defect: the double exp (`expd`, in dpmath.a68)
 * returns exactly twice the correct value for NEGATIVE arguments (a one-bit
 * exponent error in its reciprocal path).  The positive-argument path is
 * correct, so route negatives through it via exp(x) = 1 / exp(-x).
 *
 * TODO(worm68k): fix _expd upstream and drop this wrapper.
 */
double __mexp(double x) { return x < 0.0 ? 1.0 / expd(-x) : expd(x); }

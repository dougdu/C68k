#include "math_priv.h"

double nexttoward(double x, long double y) { return nextafter(x, (double)y); }

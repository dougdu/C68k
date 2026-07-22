#include "math_priv.h"

/* Round to nearest, ties away from zero. */
double round(double x) {
  return x >= 0.0 ? floord(x + 0.5) : ceild(x - 0.5);
}

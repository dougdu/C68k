#include "math_priv.h"

/* Complementary error function via the compact rational approximation
   (Numerical Recipes `erfcc`): fractional error < 1.2e-7 everywhere. */
double erfc(double x) {
  double z = __signbit(x) ? -x : x;
  double t = 1.0 / (1.0 + 0.5 * z);
  double ans =
      t * __mexp(-z * z - 1.26551223 +
               t * (1.00002368 +
                    t * (0.37409196 +
                         t * (0.09678418 +
                              t * (-0.18628806 +
                                   t * (0.27886807 +
                                        t * (-1.13520398 +
                                             t * (1.48851587 +
                                                  t * (-0.82215223 +
                                                       t * 0.17087277)))))))));
  return __signbit(x) ? 2.0 - ans : ans;
}

#include <errno.h>
#include "math_priv.h"

/* Gamma via the Lanczos approximation (g=7, 9 coefficients; ~14 digits).
   Reflection handles x < 0.5. */
#define TG_PI 3.14159265358979323846

double tgamma(double x) {
  if (x <= 0.0 && x == floord(x)) { /* poles at 0, -1, -2, ... */
    errno = EDOM;
    return __nan_val();
  }
  if (x < 0.5) /* reflection: gamma(x) = pi / (sin(pi x) * gamma(1-x)) */
    return TG_PI / (sind(TG_PI * x) * tgamma(1.0 - x));

  double z = x - 1.0;
  double a = 0.99999999999980993 + 676.5203681218851 / (z + 1.0) -
             1259.1392167224028 / (z + 2.0) + 771.32342877765313 / (z + 3.0) -
             176.61502916214059 / (z + 4.0) + 12.507343278686905 / (z + 5.0) -
             0.13857109526572012 / (z + 6.0) +
             9.9843695780195716e-6 / (z + 7.0) +
             1.5056327351493116e-7 / (z + 8.0);
  double t = z + 7.5;
  /* sqrt(2*pi) * t^(z+0.5) * e^-t * a */
  return 2.5066282746310002 * powd(t, z + 0.5) * expd(-t) * a;
}

#include <errno.h>
#include "math_priv.h"

/* log|gamma(x)| via the Lanczos approximation (log form, so it does not
   overflow for large x).  Reflection handles x < 0.5. */
#define LG_PI 3.14159265358979323846

double lgamma(double x) {
  if (x <= 0.0 && x == floord(x)) { /* poles at 0, -1, -2, ... */
    errno = ERANGE;
    return __huge_val();
  }
  if (x < 0.5) { /* lgamma(x) = log(pi/|sin(pi x)|) - lgamma(1-x) */
    double s = sind(LG_PI * x);
    if (s < 0.0)
      s = -s;
    return logd(LG_PI / s) - lgamma(1.0 - x);
  }
  double z = x - 1.0;
  double a = 0.99999999999980993 + 676.5203681218851 / (z + 1.0) -
             1259.1392167224028 / (z + 2.0) + 771.32342877765313 / (z + 3.0) -
             176.61502916214059 / (z + 4.0) + 12.507343278686905 / (z + 5.0) -
             0.13857109526572012 / (z + 6.0) +
             9.9843695780195716e-6 / (z + 7.0) +
             1.5056327351493116e-7 / (z + 8.0);
  double t = z + 7.5;
  /* 0.5*log(2*pi) + (z+0.5)*log(t) - t + log(a) */
  return 0.91893853320467274178 + (z + 0.5) * logd(t) - t + logd(a);
}

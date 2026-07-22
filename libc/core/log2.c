#include <errno.h>
#include "math_priv.h"

double log2(double x) {
  if (x < 0.0) {
    errno = EDOM;
    return __nan_val();
  }
  if (x == 0.0) {
    errno = ERANGE;
    return -__huge_val();
  }
  return logd(x) * 1.44269504088896340736; /* log(x) / ln2 */
}

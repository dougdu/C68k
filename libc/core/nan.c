#include "math_priv.h"

/* Payload tags are not supported: any tag yields a plain quiet NaN. */
double nan(const char *tag) {
  (void)tag;
  return __nan_val();
}

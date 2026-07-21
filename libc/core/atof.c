#include <stdlib.h>
#include "libc_internal.h"

/* String->double lives in the soft-float archive (returns D0:D1). atof binds
 * to atod (double) rather than the archive's single-precision _atof. */
double atof(const char *s) { return atod(s); }

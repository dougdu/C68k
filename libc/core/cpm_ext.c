#include <cpm.h>
#include "libc_internal.h"

long bdos(int func, long param) { return cpm_bdos(func, param); }

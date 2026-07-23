#include <stdlib.h>
#include "libc_internal.h"

/* Look name up in the process environment.  Osiris exposes a real environment
 * (DOS 64h, via the sys_getenv seam); CP/M-68K has none, so its seam returns
 * NULL and every lookup misses.  The returned string points into OS-owned
 * storage: the caller must not modify it (C99 7.20.4.5). */
char *getenv(const char *name) {
  return sys_getenv(name);
}

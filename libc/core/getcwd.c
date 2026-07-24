#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

/* The seam writes "X:\" plus up to a 64-byte DOS path and a NUL, so it needs a
 * buffer of at least 68 bytes. */
char *getcwd(char *buf, size_t size) {
  if (!buf || size < 68) {
    errno = ERANGE;
    return NULL;
  }
  if (sys_getcwd(buf) != 0) {
    errno = EACCES;
    return NULL;
  }
  return buf;
}

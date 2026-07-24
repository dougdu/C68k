#include <stdio.h>
#include <errno.h>
#include "libc_internal.h"

int rename(const char *oldp, const char *newp) {
  if (sys_rename(oldp, newp) != 0) {
    errno = __oserrno();
    return -1;
  }
  return 0;
}

#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

int unlink(const char *path) {
  if (sys_unlink(path) != 0) {
    errno = __oserrno();
    return -1;
  }
  return 0;
}

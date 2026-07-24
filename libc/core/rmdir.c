#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

int rmdir(const char *path) {
  if (sys_rmdir(path) != 0) {
    errno = __oserrno();
    return -1;
  }
  return 0;
}

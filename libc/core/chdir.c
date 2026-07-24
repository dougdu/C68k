#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

int chdir(const char *path) {
  if (sys_chdir(path) != 0) {
    errno = __oserrno();
    return -1;
  }
  return 0;
}

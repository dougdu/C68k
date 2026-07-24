#include <sys/stat.h>
#include <errno.h>
#include "libc_internal.h"

int mkdir(const char *path, mode_t mode) {
  (void)mode;
  if (sys_mkdir(path) != 0) {
    errno = __oserrno();
    return -1;
  }
  return 0;
}

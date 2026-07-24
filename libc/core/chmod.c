#include <sys/stat.h>
#include <errno.h>
#include "libc_internal.h"

/* These targets carry only a read-only attribute, so chmod reduces to
 * setting/clearing it: no write permission in the mode means read-only. */
int chmod(const char *path, mode_t mode) {
  int attr = (mode & 0222) ? 0x00 : 0x01;
  if (sys_chmod(path, attr) != 0) {
    errno = ENOENT;
    return -1;
  }
  return 0;
}

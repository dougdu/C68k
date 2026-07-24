#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

/* 1 if fd refers to a terminal/character device, 0 otherwise.  Osiris asks the
 * DOS IOCTL device-info service (44h/00h); CP/M treats fds 0/1/2 as the
 * console. */
int isatty(int fd) {
  int r = sys_isatty(fd);
  if (!r)
    errno = ENOTTY;
  return r;
}

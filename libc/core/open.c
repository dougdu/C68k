#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "libc_internal.h"

/* POSIX open() over the syscall seam.  O_CREAT maps to sys_creat (DOS 3Ch /
 * CP/M F_MAKE), which always creates-or-truncates; a plain open maps to
 * sys_open with the access mode.  O_APPEND positions at end-of-file.  The
 * variadic mode argument (present only with O_CREAT) is ignored: these targets
 * have no POSIX permission bits. */
int open(const char *path, int flags, ...) {
  int fd;
  if (flags & O_CREAT)
    fd = sys_creat(path, 0);
  else
    fd = sys_open(path, flags & O_ACCMODE);
  if (fd < 0) {
    errno = __oserrno();
    return -1;
  }
  if (flags & O_APPEND)
    sys_seek(fd, 0, SEEK_END);
  return fd;
}

int creat(const char *path, int mode) {
  (void)mode;
  return open(path, O_CREAT | O_WRONLY | O_TRUNC, 0);
}

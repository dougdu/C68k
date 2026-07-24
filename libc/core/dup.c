#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

/* Duplicate a descriptor.  Osiris backs this with DOS 45h/46h; CP/M-68K has no
 * descriptor duplication (files are FCB-based), so the seam fails there. */
int dup(int fd) {
  int r = sys_dup(fd);
  if (r < 0)
    errno = EBADF;
  return r;
}

int dup2(int oldfd, int newfd) {
  int r = sys_dup2(oldfd, newfd);
  if (r < 0)
    errno = EBADF;
  return r;
}

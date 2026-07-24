#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

/* Minimal fcntl(): F_DUPFD duplicates the descriptor; there is no per-fd flag
 * store on these targets, so F_GETFL reports read/write and F_GETFD/F_SETFD/
 * F_SETFL are accepted as no-ops.  The variadic third argument is unused. */
int fcntl(int fd, int cmd, ...) {
  switch (cmd) {
  case F_DUPFD:
    return dup(fd);
  case F_GETFL:
    return O_RDWR;
  case F_GETFD:
  case F_SETFD:
  case F_SETFL:
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

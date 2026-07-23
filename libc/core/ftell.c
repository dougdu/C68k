#include <stdio.h>
#include "libc_internal.h"

/* The fd position runs ahead of (buffered read) or behind (buffered write) the
 * stream's logical position by the number of buffered bytes, so correct for the
 * buffer: a read stream has `cnt` bytes still unread, a write stream has `cnt`
 * bytes buffered but not yet written. */
long ftell(FILE *fp) {
  long pos = sys_seek(fp->fd, 0, SEEK_CUR);
  if (pos < 0)
    return -1;
  return (fp->flags & _SF_WRITE) ? pos + fp->cnt : pos - fp->cnt;
}

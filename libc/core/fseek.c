#include <stdio.h>
#include "libc_internal.h"

int fseek(FILE *fp, long off, int whence) {
  fflush(fp);
  fp->cnt = 0;
  fp->p = fp->buf;
  fp->flags &= ~_SF_EOF;
  return sys_seek(fp->fd, off, whence) < 0 ? -1 : 0;
}

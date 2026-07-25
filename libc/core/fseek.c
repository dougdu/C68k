#include <stdio.h>
#include "libc_internal.h"

int fseek(FILE *fp, long off, int whence) {
  fflush(fp);
  if (!fp->base) {
    fp->base = fp->buf;
    fp->bufsize = BUFSIZ;
  }
  fp->cnt = 0;
  fp->p = fp->base;
  fp->flags &= ~_SF_EOF;
  if (fp->flags & _SF_READ) /* read/update: a seek leaves write orientation */
    fp->flags &= ~_SF_WRITING;
  return sys_seek(fp->fd, off, whence) < 0 ? -1 : 0;
}

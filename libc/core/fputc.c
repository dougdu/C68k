#include <stdio.h>
#include "libc_internal.h"

int fputc(int c, FILE *fp) {
  if (!(fp->flags & _SF_WRITE)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  /* Update stream turning around from reading: drop any read-ahead (put the fd
     back at the logical position) and enter write orientation. */
  if (!(fp->flags & _SF_WRITING)) {
    if (fp->cnt > 0)
      sys_seek(fp->fd, -(long)fp->cnt, SEEK_CUR);
    fp->cnt = 0;
    fp->p = fp->buf;
    fp->flags |= _SF_WRITING;
  }
  fp->buf[fp->cnt++] = (unsigned char)c;
  if (fp->cnt == BUFSIZ || c == '\n' || (fp->flags & _SF_NBF))
    if (fflush(fp) == EOF)
      return EOF;
  return (unsigned char)c;
}

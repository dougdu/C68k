#include <stdio.h>
#include "libc_internal.h"

/* fgetc keeps the buffer refill private. */
static int _fill(FILE *fp) {
  int n = sys_read(fp->fd, fp->buf, BUFSIZ);
  if (n <= 0) {
    fp->flags |= (n == 0) ? _SF_EOF : _SF_ERR;
    fp->cnt = 0;
    return EOF;
  }
  fp->cnt = n;
  fp->p = fp->buf;
  return 0;
}

int fgetc(FILE *fp) {
  if (!(fp->flags & _SF_READ)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  if (fp->cnt == 0 && _fill(fp) == EOF)
    return EOF;
  fp->cnt--;
  return *fp->p++;
}

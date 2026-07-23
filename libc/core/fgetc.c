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
  /* Update stream turning around from writing: flush pending output first;
     reading then continues from the resulting file position. */
  if (fp->flags & _SF_WRITING) {
    fflush(fp);
    fp->flags &= ~_SF_WRITING;
    fp->cnt = 0;
    fp->p = fp->buf;
  }
  if (fp->flags & _SF_EOF) /* sticky: stay at EOF until clearerr/rewind/seek */
    return EOF;
  if (fp->cnt == 0 && _fill(fp) == EOF)
    return EOF;
  fp->cnt--;
  int c = *fp->p++;
  /* Text streams honor the CP/M / DOS Ctrl-Z (0x1A) end-of-file marker so a
     record-padded CP/M file reads back at its logical length; binary streams
     ("...b") deliver 0x1A as an ordinary byte. */
  if (c == 0x1A && !(fp->flags & _SF_BIN)) {
    fp->flags |= _SF_EOF;
    return EOF;
  }
  return c;
}

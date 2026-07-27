#include <stdio.h>
#include "libc_internal.h"

/* fgetc keeps the buffer refill private. */
static int _fill(FILE *fp) {
  int n = sys_read(fp->fd, fp->base, fp->bufsize);
  if (n <= 0) {
    fp->flags |= (n == 0) ? _SF_EOF : _SF_ERR;
    fp->cnt = 0;
    return EOF;
  }
  fp->cnt = n;
  fp->p = fp->base;
  return 0;
}

int fgetc(FILE *fp) {
  if (!(fp->flags & _SF_READ)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  if (!fp->base) {
    fp->base = fp->buf;
    fp->bufsize = BUFSIZ;
  }
  /* Update stream turning around from writing: flush pending output first;
     reading then continues from the resulting file position. */
  if (fp->flags & _SF_WRITING) {
    fflush(fp);
    fp->flags &= ~_SF_WRITING;
    fp->cnt = 0;
    fp->p = fp->base;
  }
  if (fp->flags & _SF_EOF) /* sticky: stay at EOF until clearerr/rewind/seek */
    return EOF;
  for (;;) {
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
    /* Text-mode CR stripping -- the inverse of fputc's '\n' -> CR LF: a text
       stream reads back the logical '\n', dropping the CR of a CR LF pair (a
       bare CR is dropped too).  Binary ("...b") streams pass CR through. */
    if (c == '\r' && !(fp->flags & _SF_BIN))
      continue;
    return c;
  }
}

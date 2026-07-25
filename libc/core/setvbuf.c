#include <stdio.h>

/* Buffering control.  A caller-supplied buffer of `size` bytes is adopted for a
 * buffered stream (_IOFBF/_IOLBF); a NULL buffer keeps the stream's built-in
 * BUFSIZ buffer.  _IONBF makes the stream unbuffered (flush after every write).
 * Per C99 this must be called after the stream is opened and before any I/O.
 * Returns 0 on success, -1 on a bad argument. */
int setvbuf(FILE *fp, char *buf, int mode, size_t size) {
  if (!fp || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF))
    return -1;
  if (mode == _IONBF) {
    fp->flags |= _SF_NBF;
  } else {
    fp->flags &= ~_SF_NBF;
    if (buf && size > 0) { /* adopt the caller's buffer */
      fp->base = (unsigned char *)buf;
      fp->bufsize = (int)size;
      fp->p = fp->base;
      fp->cnt = 0;
    }
  }
  if (!fp->base) { /* stream not yet touched: fall back to the built-in buffer */
    fp->base = fp->buf;
    fp->bufsize = BUFSIZ;
  }
  return 0;
}

void setbuf(FILE *fp, char *buf) {
  setvbuf(fp, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

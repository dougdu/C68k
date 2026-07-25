#include <stdio.h>
#include <string.h>

/*
 * Push one character back onto a read stream so the next fgetc returns it.
 * C guarantees a single pushback; this also copes with the buffer being at
 * its start (before any read, or exactly at buf[0]) by shifting the pending
 * bytes up one slot.
 */
int ungetc(int c, FILE *fp) {
  if (c == EOF || !(fp->flags & _SF_READ))
    return EOF;
  if (!fp->base) {
    fp->base = fp->buf;
    fp->bufsize = BUFSIZ;
  }
  if (fp->p > fp->base) {
    fp->p--; /* room to back up over an already-served byte */
  } else {
    if (fp->cnt >= fp->bufsize)
      return EOF; /* buffer full at the front, nowhere to put it */
    if (fp->p == NULL)
      fp->p = fp->base; /* nothing read yet */
    memmove(fp->base + 1, fp->base, fp->cnt);
    fp->p = fp->base;
  }
  *fp->p = (unsigned char)c;
  fp->cnt++;
  fp->flags &= ~_SF_EOF;
  return (unsigned char)c;
}

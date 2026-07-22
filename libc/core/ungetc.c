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
  if (fp->p > fp->buf) {
    fp->p--; /* room to back up over an already-served byte */
  } else {
    if (fp->cnt >= BUFSIZ)
      return EOF; /* buffer full at the front, nowhere to put it */
    if (fp->p == NULL)
      fp->p = fp->buf; /* nothing read yet */
    memmove(fp->buf + 1, fp->buf, fp->cnt);
    fp->p = fp->buf;
  }
  *fp->p = (unsigned char)c;
  fp->cnt++;
  fp->flags &= ~_SF_EOF;
  return (unsigned char)c;
}

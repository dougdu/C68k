#include <stdio.h>

int fputc(int c, FILE *fp) {
  if (!(fp->flags & _SF_WRITE)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  fp->buf[fp->cnt++] = (unsigned char)c;
  if (fp->cnt == BUFSIZ || c == '\n')
    if (fflush(fp) == EOF)
      return EOF;
  return (unsigned char)c;
}

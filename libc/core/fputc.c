#include <stdio.h>
#include "libc_internal.h"

int fputc(int c, FILE *fp) {
  if (!(fp->flags & _SF_WRITE)) {
    fp->flags |= _SF_ERR;
    return EOF;
  }
  if (!fp->base) {
    fp->base = fp->buf;
    fp->bufsize = BUFSIZ;
  }
  /* Update stream turning around from reading: drop any read-ahead (put the fd
     back at the logical position) and enter write orientation. */
  if (!(fp->flags & _SF_WRITING)) {
    if (fp->cnt > 0)
      sys_seek(fp->fd, -(long)fp->cnt, SEEK_CUR);
    fp->cnt = 0;
    fp->p = fp->base;
    fp->flags |= _SF_WRITING;
  }
  /* Text-mode newline translation: the CP/M-family systems this libc serves
     (Osiris, CP/M-68K) end a line with CR LF, so a text stream emits '\r'
     before each '\n' -- otherwise the console drops to the next row without
     returning to column 0, and text files lack DOS line endings.  Binary
     ("...b", _SF_BIN) and in-memory (open_memstream, _SF_MEM) streams are
     exempt; the inverse Ctrl-Z text-EOF handling lives in fgetc. */
  if (c == '\n' && !(fp->flags & (_SF_BIN | _SF_MEM))) {
    fp->base[fp->cnt++] = '\r';
    if (fp->cnt == fp->bufsize)
      if (fflush(fp) == EOF)
        return EOF;
  }
  fp->base[fp->cnt++] = (unsigned char)c;
  if (fp->cnt == fp->bufsize || c == '\n' || (fp->flags & _SF_NBF))
    if (fflush(fp) == EOF)
      return EOF;
  return (unsigned char)c;
}

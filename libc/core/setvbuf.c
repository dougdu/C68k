#include <stdio.h>

/* Buffering control.  The stream always uses its built-in BUFSIZ buffer, so a
 * caller-supplied buffer is accepted but ignored; only the mode is honored:
 * _IONBF flushes after every write, _IOFBF/_IOLBF keep the built-in buffering
 * (flush on full, and on newline for text).  Returns 0 on success, -1 on a bad
 * argument. */
int setvbuf(FILE *fp, char *buf, int mode, size_t size) {
  (void)buf;
  (void)size;
  if (!fp || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF))
    return -1;
  if (mode == _IONBF)
    fp->flags |= _SF_NBF;
  else
    fp->flags &= ~_SF_NBF;
  return 0;
}

void setbuf(FILE *fp, char *buf) {
  setvbuf(fp, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

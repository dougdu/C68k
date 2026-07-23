#include <stdio.h>
#include "libc_internal.h"

int fclose(FILE *fp) {
  if (!fp || !(fp->flags & _SF_USED))
    return EOF;
  fflush(fp);
  int r = 0;
  if (fp->drain) {
    if (!fp->mem) /* empty stream still yields a valid buffer */
      fp->drain(fp, "", 0);
    if (fp->memuptr)
      *fp->memuptr = (char *)fp->mem;
    if (fp->memusize)
      *fp->memusize = fp->memlen;
  } else {
    r = sys_close(fp->fd);
    if (fp->flags & _SF_TMP) /* tmpfile: remove the backing file on close */
      sys_unlink(fp->tmpname);
  }
  fp->flags = 0;
  fp->drain = 0; /* release the slot's memstream hook, if any */
  return r;
}

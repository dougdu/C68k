#include <stdio.h>
#include "libc_internal.h"

int fflush(FILE *fp) {
  if (!fp) {
    for (int i = 0; i < NSTREAM; i++)
      if ((_streams[i].flags & (_SF_USED | _SF_WRITE)) == (_SF_USED | _SF_WRITE))
        fflush(&_streams[i]);
    return 0;
  }
  if ((fp->flags & _SF_WRITING) && fp->cnt > 0) {
    if (fp->drain) {
      if (fp->drain(fp, fp->buf, fp->cnt) != 0) {
        fp->flags |= _SF_ERR;
        return EOF;
      }
    } else if (sys_write(fp->fd, fp->buf, fp->cnt) != fp->cnt) {
      fp->flags |= _SF_ERR;
      return EOF;
    }
    fp->cnt = 0;
  }
  return 0;
}

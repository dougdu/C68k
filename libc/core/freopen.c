#include <stdio.h>
#include "libc_internal.h"

/* Flush and close the stream's file, then reopen `path` in `mode` reusing the
 * same FILE object -- so freopen("out.txt","w",stdout) redirects stdout.  Per
 * C99 the stream is closed first; on any failure the stream is left closed and
 * NULL is returned.  A NULL path (mode change of the existing file) is not
 * supported here and fails. */
FILE *freopen(const char *path, const char *mode, FILE *fp) {
  if (!fp)
    return NULL;
  if (fp->flags & _SF_USED) {
    fflush(fp);
    if (!fp->drain) /* memstreams have no real fd to close */
      sys_close(fp->fd);
  }
  fp->flags = 0;
  fp->drain = 0;
  if (!path)
    return NULL;
  return _fopen_fp(fp, path, mode);
}

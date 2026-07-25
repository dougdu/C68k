#include <stdio.h>
#include "libc_internal.h"

/* Flush and close the stream's file, then reopen `path` in `mode` reusing the
 * same FILE object -- so freopen("out.txt","w",stdout) redirects stdout.  Per
 * C99 the stream is closed first; on any failure the stream is left closed and
 * NULL is returned.  A NULL path changes the mode of the already-open stream
 * in place (see below). */
FILE *freopen(const char *path, const char *mode, FILE *fp) {
  if (!fp)
    return NULL;

  /* NULL path: change the mode of the already-open stream in place (no
     re-open) -- keep the fd, flush, re-derive the read/write/binary flags from
     `mode`, and reset the buffer.  Used mainly to switch a stream between text
     and binary.  (Widening access beyond the fd's original open -- e.g. a
     read-only file to "w" -- is not honored by the underlying handle.) */
  if (!path) {
    if (!(fp->flags & _SF_USED))
      return NULL;
    fflush(fp);
    int upd = 0, bin = 0, flags;
    for (const char *m = mode; *m; m++) {
      if (*m == '+')
        upd = 1;
      else if (*m == 'b')
        bin = 1;
    }
    if (mode[0] == 'r')
      flags = upd ? (_SF_READ | _SF_WRITE) : _SF_READ;
    else if (mode[0] == 'w' || mode[0] == 'a')
      flags = upd ? (_SF_READ | _SF_WRITE) : _SF_WRITE;
    else
      return NULL;
    if (bin)
      flags |= _SF_BIN;
    if ((flags & (_SF_READ | _SF_WRITE)) == _SF_WRITE)
      flags |= _SF_WRITING;
    if (!fp->base) {
      fp->base = fp->buf;
      fp->bufsize = BUFSIZ;
    }
    fp->flags = flags | _SF_USED;
    fp->cnt = 0;
    fp->p = fp->base;
    if (mode[0] == 'a')
      sys_seek(fp->fd, 0, SEEK_END);
    return fp;
  }

  if (fp->flags & _SF_USED) {
    fflush(fp);
    if (!fp->drain) /* memstreams have no real fd to close */
      sys_close(fp->fd);
  }
  fp->flags = 0;
  fp->drain = 0;
  return _fopen_fp(fp, path, mode);
}

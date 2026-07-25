#include <stdio.h>
#include <errno.h>
#include "libc_internal.h"

/* Wrap an already-open descriptor (from open()/dup()/etc.) in a buffered
 * stream.  Mirrors _fopen_fp's flag parsing but takes the descriptor as given
 * instead of calling sys_open/sys_creat. */
FILE *fdopen(int fd, const char *mode) {
  FILE *fp = NULL;
  for (int i = 3; i < NSTREAM; i++)
    if (!(_streams[i].flags & _SF_USED)) {
      fp = &_streams[i];
      break;
    }
  if (!fp) {
    errno = EMFILE;
    return NULL;
  }

  int upd = 0, flags;
  for (const char *m = mode; *m; m++)
    if (*m == '+')
      upd = 1;
  if (mode[0] == 'r') {
    flags = upd ? (_SF_READ | _SF_WRITE) : _SF_READ;
  } else if (mode[0] == 'w') {
    flags = upd ? (_SF_READ | _SF_WRITE) : _SF_WRITE;
  } else if (mode[0] == 'a') {
    flags = upd ? (_SF_READ | _SF_WRITE) : _SF_WRITE;
    sys_seek(fd, 0, SEEK_END);
  } else {
    errno = EINVAL;
    return NULL;
  }
  for (const char *m = mode; *m; m++)
    if (*m == 'b')
      flags |= _SF_BIN;
  if ((flags & (_SF_READ | _SF_WRITE)) == _SF_WRITE)
    flags |= _SF_WRITING;

  fp->fd = fd;
  fp->flags = flags | _SF_USED;
  fp->drain = 0;
  fp->cnt = 0;
  fp->base = fp->buf;
  fp->bufsize = BUFSIZ;
  fp->p = fp->base;
  return fp;
}

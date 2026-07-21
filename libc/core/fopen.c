#include <stdio.h>
#include <errno.h>
#include "libc_internal.h"

FILE *fopen(const char *path, const char *mode) {
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

  int fd, flags;
  if (mode[0] == 'r') {
    fd = sys_open(path, 0);
    flags = _SF_READ;
  } else if (mode[0] == 'w') {
    fd = sys_creat(path, 0);
    flags = _SF_WRITE;
  } else if (mode[0] == 'a') {
    fd = sys_open(path, 1);
    if (fd < 0)
      fd = sys_creat(path, 0);
    if (fd >= 0)
      sys_seek(fd, 0, SEEK_END);
    flags = _SF_WRITE;
  } else {
    errno = EINVAL;
    return NULL;
  }
  if (fd < 0) {
    errno = ENOENT;
    return NULL;
  }

  fp->fd = fd;
  fp->flags = flags | _SF_USED;
  fp->drain = 0; /* a fresh file stream is not a memstream */
  fp->cnt = 0;
  fp->p = fp->buf;
  return fp;
}

#include <stdio.h>
#include <errno.h>
#include "libc_internal.h"

FILE *_fopen_fp(FILE *fp, const char *path, const char *mode) {
  int upd = 0;
  for (const char *m = mode; *m; m++)
    if (*m == '+')
      upd = 1;

  int fd, flags;
  if (mode[0] == 'r') {
    fd = sys_open(path, upd ? 2 : 0); /* 2 = read/write */
    flags = upd ? (_SF_READ | _SF_WRITE) : _SF_READ;
  } else if (mode[0] == 'w') {
    fd = sys_creat(path, 0); /* create/truncate; the handle is read/write */
    flags = upd ? (_SF_READ | _SF_WRITE) : _SF_WRITE;
  } else if (mode[0] == 'a') {
    fd = sys_open(path, upd ? 2 : 1);
    if (fd < 0)
      fd = sys_creat(path, 0);
    if (fd >= 0)
      sys_seek(fd, 0, SEEK_END);
    flags = upd ? (_SF_READ | _SF_WRITE) : _SF_WRITE;
  } else {
    errno = EINVAL;
    return NULL;
  }
  /* A 'b' anywhere in the mode selects binary: on CP/M (and by the DOS text
     convention) a text stream stops at a Ctrl-Z (0x1A); binary reads it raw. */
  for (const char *m = mode; *m; m++)
    if (*m == 'b')
      flags |= _SF_BIN;
  if (fd < 0) {
    errno = ENOENT;
    return NULL;
  }

  /* A pure write stream is always in write orientation; read and update streams
     start reading (a later fputc turns an update stream around to writing). */
  if ((flags & (_SF_READ | _SF_WRITE)) == _SF_WRITE)
    flags |= _SF_WRITING;

  fp->fd = fd;
  fp->flags = flags | _SF_USED;
  fp->drain = 0; /* a fresh file stream is not a memstream */
  fp->cnt = 0;
  fp->p = fp->buf;
  return fp;
}

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
  return _fopen_fp(fp, path, mode);
}

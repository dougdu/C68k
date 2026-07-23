#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc_internal.h"

/* open_memstream() growth: append n bytes to fp->mem, keeping it NUL-
 * terminated, and republish the user's *ptr / *sizeloc.  Returns 0 / -1.
 * Installed as the stream's drain hook so fflush/fclose (stdio core) reach
 * realloc only when a program actually opens a memstream. */
static int _memstream_append(FILE *fp, const void *data, int n) {
  size_t need = fp->memlen + (size_t)n + 1;
  if (need > fp->memcap) {
    size_t ncap = fp->memcap ? fp->memcap : 64;
    while (ncap < need)
      ncap *= 2;
    unsigned char *nm = realloc(fp->mem, ncap);
    if (!nm)
      return -1;
    fp->mem = nm;
    fp->memcap = ncap;
  }
  if (n)
    memcpy(fp->mem + fp->memlen, data, n);
  fp->memlen += n;
  fp->mem[fp->memlen] = 0;
  if (fp->memuptr)
    *fp->memuptr = (char *)fp->mem;
  if (fp->memusize)
    *fp->memusize = fp->memlen;
  return 0;
}

/* POSIX open_memstream(): a write stream backed by a malloc'd buffer that
 * grows on flush; on fclose the caller owns *ptr (size in *sizeloc). */
FILE *open_memstream(char **ptr, size_t *sizeloc) {
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
  fp->fd = -1;
  fp->flags = _SF_WRITE | _SF_WRITING | _SF_MEM | _SF_USED;
  fp->drain = _memstream_append; /* stdio core drains memstreams via this hook */
  fp->cnt = 0;
  fp->p = fp->buf;
  fp->mem = NULL;
  fp->memcap = 0;
  fp->memlen = 0;
  fp->memuptr = ptr;
  fp->memusize = sizeloc;
  if (ptr)
    *ptr = NULL;
  if (sizeloc)
    *sizeloc = 0;
  return fp;
}

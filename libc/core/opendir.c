#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include "libc_internal.h"

/* The find block is the DOS 4Eh DTA (43 bytes); malloc keeps the DIR (and thus
 * the block at offset 0) even-aligned, which the DOS word/long field stores
 * require.  Only one directory scan is active per DIR; on CP/M the BDOS keeps a
 * single search state, so concurrent scans are not supported there. */
struct __dirstream {
  unsigned char find[48];
  struct dirent de;
  char pattern[80];
  int state; /* 0 = need find-first, 1 = iterating, 2 = ended */
};

DIR *opendir(const char *path) {
  DIR *d = malloc(sizeof *d);
  if (!d) {
    errno = ENOMEM;
    return NULL;
  }
  /* Build a "<path>\*.*" search pattern; "." means the current directory. */
  int n = 0;
  for (const char *p = path; *p && n < 72; p++)
    d->pattern[n++] = *p;
  if (n && (d->pattern[n - 1] == '/' || d->pattern[n - 1] == '\\'))
    n--; /* drop a trailing separator */
  if (n == 1 && d->pattern[0] == '.')
    n = 0; /* "." -> current directory */
  if (n)
    d->pattern[n++] = '\\';
  d->pattern[n++] = '*';
  d->pattern[n++] = '.';
  d->pattern[n++] = '*';
  d->pattern[n] = 0;
  d->state = 0;
  return d;
}

struct dirent *readdir(DIR *d) {
  int r;
  if (d->state == 0) {
    r = sys_findfirst(d->pattern, 0x16, d->find);
    d->state = (r == 0) ? 1 : 2;
  } else if (d->state == 1) {
    r = sys_findnext(d->find);
    if (r != 0)
      d->state = 2;
  } else {
    r = -1;
  }
  if (r != 0)
    return NULL;
  const char *nm = (const char *)&d->find[30]; /* ASCIIZ name at offset 0x1E */
  int i = 0;
  while (nm[i] && i < 15) {
    d->de.d_name[i] = nm[i];
    i++;
  }
  d->de.d_name[i] = 0;
  d->de.d_ino = 0;
  return &d->de;
}

int closedir(DIR *d) {
  if (!d) {
    errno = EBADF;
    return -1;
  }
  free(d);
  return 0;
}

void rewinddir(DIR *d) {
  if (d)
    d->state = 0;
}

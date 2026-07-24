#ifndef _DIRENT_H
#define _DIRENT_H

#include <stddef.h>

/* Directory entry.  These targets use 8.3 names, so d_name is short; d_ino has
 * no meaning here and is always 0. */
struct dirent {
  long d_ino;
  char d_name[16];
};

typedef struct __dirstream DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);

#endif /* _DIRENT_H */

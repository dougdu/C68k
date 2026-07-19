#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>
#include <time.h>

/* Minimal stat buffer.  On these targets the native self-host build has no
 * general stat() service, so stat() is a stub that fails (see libc.c); the
 * only field the compiler reads is st_mtime (for __TIMESTAMP__, which then
 * falls back to its "unknown" form). */
struct stat {
  long   st_mode;
  long   st_size;
  time_t st_mtime;
  time_t st_atime;
  time_t st_ctime;
};

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);

#endif /* _SYS_STAT_H */

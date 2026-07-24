#ifndef _UTIME_H
#define _UTIME_H

#include <time.h>

struct utimbuf {
  time_t actime;  /* access time (ignored: no atime on these targets) */
  time_t modtime; /* modification time */
};

int utime(const char *path, const struct utimbuf *times);

#endif /* _UTIME_H */

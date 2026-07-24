#include <utime.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "libc_internal.h"

extern void __civil_from_days(long z, int *py, int *pm, int *pd);

/* Set a file's modification time.  The handle-based DOS 57h/01 service sets the
 * directory date/time, so open the path, set, and close.  CP/M-68K has no
 * per-file timestamp, so sys_setfiletime fails there. */
int utime(const char *path, const struct utimbuf *times) {
  time_t mt = times ? times->modtime : time(NULL);
  long days = mt / 86400L;
  long secs = mt % 86400L;
  int y, mo, dy;
  __civil_from_days(days, &y, &mo, &dy);
  int hh = (int)(secs / 3600);
  int mi = (int)((secs % 3600) / 60);
  int ss = (int)(secs % 60);
  unsigned date = (((y - 1980) & 0x7F) << 9) | ((mo & 0x0F) << 5) | (dy & 0x1F);
  unsigned tm = ((hh & 0x1F) << 11) | ((mi & 0x3F) << 5) | ((ss / 2) & 0x1F);
  long dtstamp = ((long)date << 16) | tm;

  /* Open read-only: 57h/01 writes the directory date/time directly, and a
   * read handle's close will not refresh the timestamp to the current time. */
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    errno = ENOENT;
    return -1;
  }
  int r = sys_setfiletime(fd, dtstamp);
  close(fd);
  if (r != 0) {
    errno = EACCES;
    return -1;
  }
  return 0;
}

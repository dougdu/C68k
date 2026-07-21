#include <sys/stat.h>
#include <errno.h>

/* No general stat() on these targets: fail so __TIMESTAMP__ takes its
 * "unknown" fallback (the compiler reads nothing else from stat). */
int stat(const char *path, struct stat *st) {
  (void)path;
  (void)st;
  errno = EINVAL;
  return -1;
}

int fstat(int fd, struct stat *st) {
  (void)fd;
  (void)st;
  errno = EINVAL;
  return -1;
}

#include <unistd.h>
#include <errno.h>
#include "libc_internal.h"

/* Test file accessibility.  sys_access returns a DOS-style attribute word
 * (bit 0 = read-only) or -1 if the file does not exist; both backends map their
 * native metadata onto that word so this wrapper stays OS-agnostic.  There are
 * no execute bits on these targets, so X_OK/R_OK/F_OK reduce to existence. */
int access(const char *path, int mode) {
  int attr = sys_access(path);
  if (attr < 0) {
    errno = ENOENT;
    return -1;
  }
  if ((mode & W_OK) && (attr & 0x01)) {
    errno = EACCES;
    return -1;
  }
  return 0;
}

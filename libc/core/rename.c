#include <stdio.h>
#include <errno.h>

/* The OS seam exposes no rename service yet, so this always fails.  Kept for
 * source portability; promote to a real syscall when the seam gains one. */
int rename(const char *oldp, const char *newp) {
  (void)oldp;
  (void)newp;
  errno = EINVAL;
  return -1;
}

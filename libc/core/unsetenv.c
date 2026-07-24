#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc_internal.h"

/* Remove a variable (a no-op if it is absent).  The SET seam treats an empty
 * value as a delete. */
int unsetenv(const char *name) {
  if (!name || !*name || strchr(name, '=')) {
    errno = EINVAL;
    return -1;
  }
  sys_setenv(name, "");
  envbuild();
  return 0;
}

/* Remove every variable.  Each delete reallocates the block, so repeatedly take
 * the first variable's name and delete it until the block is empty (guarded
 * against a runaway loop). */
int clearenv(void) {
  for (int guard = 0; guard < 256; guard++) {
    char *blk = sys_getenvblk();
    if (!blk || !*blk)
      break;
    char name[128];
    int i = 0;
    for (char *p = blk; *p && *p != '=' && i < 127; p++)
      name[i++] = *p;
    name[i] = 0;
    if (!name[0])
      break;
    if (sys_setenv(name, "") != 0)
      break;
  }
  envbuild();
  return 0;
}

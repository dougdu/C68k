#include "libc_internal.h"

/* The environment array.  It points at a fixed-size static table (so pulling
 * environ into a program costs no malloc), which is (re)filled from the OS
 * environment block.  Entries point into that OS-owned block, so the table is
 * rebuilt once at startup (crt0 calls envbuild) and again after any
 * setenv/putenv/unsetenv (the Osiris kernel reallocates the block on change).
 * On CP/M-68K there is no environment, so the block is NULL and environ is
 * an empty { NULL }. */
#define MAXENV 64
static char *_envp_arr[MAXENV + 1];
char **environ = _envp_arr;

char **envbuild(void) {
  char *blk = sys_getenvblk();
  int i = 0;
  if (blk) {
    char *p = blk;
    while (*p && i < MAXENV) {
      _envp_arr[i++] = p;
      while (*p) /* skip "NAME=VALUE" */
        p++;
      p++; /* step over the NUL to the next entry */
    }
  }
  _envp_arr[i] = 0;
  environ = _envp_arr;
  return _envp_arr;
}

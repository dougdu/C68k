#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc_internal.h"

/* Add or change a variable.  The OS owns the environment storage, so the value
 * is copied in by the SET seam; environ is rebuilt afterwards because the block
 * moves on every change.  Note (platform limitation): the OS treats an empty
 * value as a delete, so setenv(name, "") removes the variable rather than
 * setting it to the empty string. */
int setenv(const char *name, const char *value, int overwrite) {
  if (!name || !*name || strchr(name, '=')) {
    errno = EINVAL;
    return -1;
  }
  if (!overwrite && getenv(name))
    return 0;
  if (sys_setenv(name, value ? value : "") != 0) {
    errno = ENOMEM;
    return -1;
  }
  envbuild();
  return 0;
}

/* putenv("NAME=VALUE") splits at '=' and sets NAME to VALUE.  Unlike glibc the
 * string does not become part of the environment (the OS keeps its own copy).
 * A bare "NAME" (no '=') removes the variable. */
int putenv(char *string) {
  if (!string) {
    errno = EINVAL;
    return -1;
  }
  char *eq = strchr(string, '=');
  if (!eq)
    return unsetenv(string);
  char name[128];
  int n = (int)(eq - string);
  if (n <= 0 || n >= (int)sizeof(name)) {
    errno = EINVAL;
    return -1;
  }
  for (int i = 0; i < n; i++)
    name[i] = string[i];
  name[n] = 0;
  if (sys_setenv(name, eq + 1) != 0) {
    errno = ENOMEM;
    return -1;
  }
  envbuild();
  return 0;
}

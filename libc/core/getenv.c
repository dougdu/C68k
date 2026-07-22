#include <stdlib.h>

/* These targets expose no process environment: every lookup misses. */
char *getenv(const char *name) {
  (void)name;
  return NULL;
}

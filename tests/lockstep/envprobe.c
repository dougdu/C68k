/* envprobe.c -- Osiris-only getenv() positive test.
 *
 * Osiris exposes a real process environment (DOS 64h): the shell publishes
 * COMSPEC = its own load path at startup (MS-DOS parity), which every child
 * inherits.  This test proves getenv() actually retrieves that value over the
 * sys_getenv seam, and that an unset name (and the empty name) miss cleanly.
 *
 * Run:  tools/osiris/run-osiris.ps1 -Src tests/lockstep/envprobe.c -Expect 'ENVPROBE PASS 4/4'
 * (Not a lockstep case: CP/M-68K has no environment, so COMSPEC is NULL there.)
 */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int pass = 0, total = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

  char *cs = getenv("COMSPEC");
  CHECK(cs != NULL);                         /* the shell always sets COMSPEC */
  CHECK(cs && cs[0] != '\0');                /* ... to a non-empty path */
  CHECK(getenv("C68K_NO_SUCH_VAR") == NULL); /* unset name misses */
  CHECK(getenv("") == NULL);                 /* empty name misses */

  printf("COMSPEC=[%s]\n", cs ? cs : "(null)");
  printf("ENVPROBE PASS %d/%d\n", pass, total);
  return 0;
}

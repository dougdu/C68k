/* P4 environment: setenv/putenv/unsetenv/clearenv + environ + main's envp.
 * Cross-OS lockstep test written as dual-target invariants (Osiris has a real
 * environment via DOS 64h; CP/M-68K has none), so the check count matches on
 * both OSes.  Prints "ENVTEST PASS n/n". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* environ */

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

int main(int argc, char **argv, char **envp) {
  (void)argc;
  (void)argv;
  int he = (getenv("COMSPEC") != NULL); /* true on Osiris, false on CP/M */

  /* environ always exists; crt0 passes it as main's envp. */
  CHECK(environ != NULL);
  CHECK(envp == environ);
  /* it is non-empty iff the platform has an environment. */
  CHECK((environ[0] != NULL) == he);

  /* setenv/getenv/environ agree, and succeed iff there is an environment. */
  CHECK((setenv("C68KENV", "abc", 1) == 0) == he);
  CHECK((getenv("C68KENV") != NULL) == he);
  int found = 0;
  for (char **e = environ; *e; e++)
    if (strncmp(*e, "C68KENV=", 8) == 0)
      found = 1;
  CHECK(found == he);

  /* no-overwrite keeps the existing value (only meaningful where env exists). */
  setenv("C68KENV", "xyz", 0);
  CHECK(!he || strcmp(getenv("C68KENV"), "abc") == 0);
  setenv("C68KENV", "xyz", 1);
  CHECK(!he || strcmp(getenv("C68KENV"), "xyz") == 0);

  /* putenv. */
  CHECK((putenv("C68KPUT=42") == 0) == he);
  CHECK(!he || strcmp(getenv("C68KPUT"), "42") == 0);

  /* unsetenv removes it -> NULL on both (Osiris: deleted; CP/M: never set). */
  unsetenv("C68KENV");
  CHECK(getenv("C68KENV") == NULL);

  printf("ENVTEST PASS %d/%d\n", pass, total);
  return 0;
}

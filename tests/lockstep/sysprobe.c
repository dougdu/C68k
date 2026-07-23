/* sysprobe.c -- Osiris-only system() positive test.
 *
 * Osiris runs commands through the command processor (COMSPEC) via DOS 4Bh
 * EXEC: system(NULL) reports one is available, and system("ECHO ...") spawns
 * COMMAND.PRG "/C ECHO ...", whose output reaches the console and whose exit
 * code (0) is returned.  A bad command prints an error but still exits cleanly.
 *
 * Run: run-osiris.ps1 -Src tests/lockstep/sysprobe.c -Expect 'SYSPROBE PASS 3/3'
 * (Not a lockstep case: CP/M-68K has no command processor, so system(NULL)=0
 * and any command returns -1 there.)
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

  CHECK(system(NULL) != 0);              /* a command processor is available */
  CHECK(system("ECHO SYSTEM_RAN") == 0); /* runs a builtin; output below, exit 0 */
  CHECK(system("TYPE A:\\CONFIG.SYS") == 0); /* runs a command that does file I/O */

  printf("SYSPROBE PASS %d/%d\n", pass, total);
  return 0;
}

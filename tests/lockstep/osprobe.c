/* Osiris-only: <osiris.h> direct DOS access (intdos trampoline + _dos_* wrappers
 * over DOS 19h/0Eh/36h).  Prints "OSPROBE PASS n/n"; run via run-osiris.ps1. */
#include <osiris.h>
#include <string.h>
#include <stdio.h>

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

int main(void) {
  int d = _dos_getdrive();
  CHECK(d >= 0 && d < 26);

  long fr = _dos_getdiskfree(0); /* default drive */
  CHECK(fr > 0);

  /* Generic escape hatch: DOS 30h (get version) must complete without error. */
  struct DOSREGS r;
  memset(&r, 0, sizeof r);
  r.d0 = 0x3000;
  intdos(&r);
  CHECK(r.cflag == 0);

  printf("OSPROBE PASS %d/%d\n", pass, total);
  return 0;
}

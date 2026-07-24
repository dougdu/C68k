/* CP/M-only: <cpm.h> direct BDOS access.  Prints "CPMPROBE PASS n/n"; run via
 * run-cpm.ps1. */
#include <cpm.h>
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
  long d0 = bdos(25, 0) & 0xFF; /* 25 = return current drive (0 = A) */
  CHECK(d0 < 16);

  bdos(14, d0); /* 14 = select disk -- select the same drive back */
  long d1 = bdos(25, 0) & 0xFF;
  CHECK(d1 == d0); /* round-trip */

  printf("CPMPROBE PASS %d/%d\n", pass, total);
  return 0;
}

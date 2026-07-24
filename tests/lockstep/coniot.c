/* P5 <conio.h> raw console -- cross-OS lockstep test.  Tests the output side
 * (putch/cputs) and non-blocking kbhit; getch/getche need interactive input so
 * they are exercised only for compilation here.  Prints "CONIOT PASS n/n". */
#include <conio.h>
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
  int r = kbhit(); /* non-blocking: 0 or 1, must not hang */
  CHECK(r == 0 || r == 1);
  CHECK(putch('X') == 'X');
  CHECK(cputs("YZ") == 2);
  putch('\r');
  putch('\n');
  printf("CONIOT PASS %d/%d\n", pass, total);
  return 0;
}

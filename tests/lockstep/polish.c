/* P6 conformance polish -- math EDOM/ERANGE, errno mapping, locale.  Cross-OS
 * lockstep test.  Prints "POLISH PASS n/n". */
#include <math.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
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
  volatile double s;

  /* math domain/range errors set errno. */
  errno = 0;
  s = sqrt(-1.0);
  CHECK(errno == EDOM);
  errno = 0;
  s = log(0.0);
  CHECK(errno == ERANGE);
  errno = 0;
  s = log(-2.0);
  CHECK(errno == EDOM);
  errno = 0;
  s = acos(2.0);
  CHECK(errno == EDOM);
  errno = 0;
  s = asin(-3.0);
  CHECK(errno == EDOM);
  errno = 0;
  s = fmod(5.0, 0.0);
  CHECK(errno == EDOM);
  errno = 0;
  s = atanh(2.0); /* Tier2 -- already sets errno */
  CHECK(errno == EDOM);
  errno = 0;
  s = sqrt(4.0); /* valid input leaves errno untouched */
  CHECK(errno == 0);
  (void)s;

  /* errno mapping: a missing file maps to ENOENT on both OSes. */
  errno = 0;
  int fd = open("NOSUCH.XYZ", O_RDONLY);
  CHECK(fd < 0 && errno == ENOENT);

  /* locale: only the "C" locale is available. */
  CHECK(setlocale(LC_ALL, NULL) != NULL);
  CHECK(strcmp(setlocale(LC_ALL, "C"), "C") == 0);
  CHECK(setlocale(LC_ALL, "de_DE.UTF-8") == NULL);
  struct lconv *lc = localeconv();
  CHECK(lc != NULL && strcmp(lc->decimal_point, ".") == 0);

  printf("POLISH PASS %d/%d\n", pass, total);
  return 0;
}

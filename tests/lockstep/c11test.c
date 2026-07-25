/* P8 C11/C17 hosted additions -- aligned_alloc/posix_memalign, at_quick_exit,
 * timespec_get, <uchar.h> UTF-8<->UTF-16/32, <fenv.h> stubs.  Cross-OS lockstep
 * test.  Prints "C11TEST PASS n/n". */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <uchar.h>
#include <fenv.h>
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

static void qhandler(void) {}

int main(void) {
  /* aligned_alloc / posix_memalign */
  void *ap = aligned_alloc(8, 64);
  CHECK(ap != 0 && ((unsigned long)ap % 8) == 0);
  free(ap);
  errno = 0;
  CHECK(aligned_alloc(3, 9) == 0 && errno == EINVAL); /* not a power of two */
  void *pp = 0;
  CHECK(posix_memalign(&pp, 8, 40) == 0 && pp && ((unsigned long)pp % 8) == 0);
  free(pp);

  /* at_quick_exit registration (quick_exit itself terminates) */
  CHECK(at_quick_exit(qhandler) == 0);

  /* timespec_get */
  struct timespec ts;
  CHECK(timespec_get(&ts, TIME_UTC) == TIME_UTC && ts.tv_sec > 0 &&
        ts.tv_nsec == 0);

  /* <uchar.h> UTF-8 <-> UTF-32 (U+1F600 emoji, U+00E9, ASCII) */
  const char *emoji = "\xF0\x9F\x98\x80";
  char32_t c32;
  char utf8[8];
  mbstate_t st;
  memset(&st, 0, sizeof st);
  CHECK(mbrtoc32(&c32, emoji, 4, &st) == 4 && c32 == 0x1F600);
  memset(&st, 0, sizeof st);
  CHECK(c32rtomb(utf8, 0x1F600, &st) == 4 && memcmp(utf8, emoji, 4) == 0);
  memset(&st, 0, sizeof st);
  CHECK(mbrtoc32(&c32, "\xC3\xA9", 2, &st) == 2 && c32 == 0xE9);
  memset(&st, 0, sizeof st);
  CHECK(mbrtoc32(&c32, "A", 1, &st) == 1 && c32 == 'A');

  /* <uchar.h> UTF-16 surrogate pair */
  char16_t c16;
  memset(&st, 0, sizeof st);
  CHECK(mbrtoc16(&c16, emoji, 4, &st) == 4 && c16 == 0xD83D); /* high */
  CHECK(mbrtoc16(&c16, emoji, 4, &st) == (size_t)-3 && c16 == 0xDE00); /* low */
  memset(&st, 0, sizeof st);
  CHECK(c16rtomb(utf8, 0xD83D, &st) == 0); /* high surrogate held */
  CHECK(c16rtomb(utf8, 0xDE00, &st) == 4 && memcmp(utf8, emoji, 4) == 0);

  /* <fenv.h> -- real sticky flags + directed rounding via the libm _fe_* ABI */
  CHECK(fegetround() == FE_TONEAREST);
  CHECK(fesetround(FE_UPWARD) == 0);        /* directed rounding is supported  */
  CHECK(fegetround() == FE_UPWARD);
  CHECK(fesetround(FE_TONEAREST) == 0);     /* restore round-to-nearest        */
  CHECK(fesetround(99) == -1);              /* an unsupported mode is rejected */
  feclearexcept(FE_ALL_EXCEPT);
  CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
  feraiseexcept(FE_INVALID | FE_INEXACT);
  CHECK(fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_INEXACT));
  CHECK(fetestexcept(FE_INVALID) == FE_INVALID);
  feclearexcept(FE_INVALID);
  CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_INEXACT);
  feclearexcept(FE_ALL_EXCEPT);
  { volatile double a = 1.0, b = 3.0, q = a / b; (void)q; } /* inexact -> flag */
  CHECK((fetestexcept(FE_INEXACT) & FE_INEXACT) != 0);
  feclearexcept(FE_ALL_EXCEPT);

  printf("C11TEST PASS %d/%d\n", pass, total);
  return 0;
}

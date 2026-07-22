/* Tier 1 C99 conformance additions -- self-checking smoke test.
 * Prints "TIER1 PASS n/n" when every check holds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

static int vs(char *b, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  int n = vsprintf(b, f, ap);
  va_end(ap);
  return n;
}

int main(void) {
  char buf[64];

  /* <string.h>: span / find-set */
  const char *s = "hello world";
  CHECK(strspn("abcxyz", "abc") == 3);
  CHECK(strcspn("abcxyz", "xyz") == 3);
  CHECK(strpbrk(s, "aeiou") == s + 1); /* 'e' */
  CHECK(strpbrk("xyz", "abc") == NULL);

  /* <ctype.h> */
  CHECK(isblank(' ') && isblank('\t') && !isblank('z'));
  CHECK(isgraph('A') && !isgraph(' ') && !isgraph('\n'));

  /* <stdlib.h>: 64-bit integer utilities */
  CHECK(atoll("9000000000") == 9000000000LL);
  CHECK(llabs(-5000000000LL) == 5000000000LL);
  lldiv_t q = lldiv(20000000001LL, 7LL);
  CHECK(q.quot == 20000000001LL / 7 && q.rem == 20000000001LL % 7);
  CHECK(strtof("3.5", NULL) == 3.5f);
  CHECK(getenv("PATH") == NULL);
  CHECK(system(NULL) == 0);

  /* <inttypes.h> */
  CHECK(imaxabs((intmax_t)-1234567890123LL) == 1234567890123LL);
  imaxdiv_t im = imaxdiv(100, 7);
  CHECK(im.quot == 14 && im.rem == 2);
  CHECK(strtoimax("-42", NULL, 10) == -42);
  CHECK(strtoumax("ff", NULL, 16) == 255);
  vs(buf, "%" PRId64, (int64_t)-9000000000LL);
  CHECK(strcmp(buf, "-9000000000") == 0);
  vs(buf, "%" PRIx32, (uint32_t)0xABCD);
  CHECK(strcmp(buf, "abcd") == 0);

  /* <errno.h>: the three C99-required macros exist and are distinct */
  CHECK(EDOM != ERANGE && ERANGE != EILSEQ && EDOM != 0);

  /* vsprintf */
  CHECK(vs(buf, "%d-%s", 7, "x") == 3 && strcmp(buf, "7-x") == 0);

  /* scanf hh length modifier (vsscanf) */
  signed char sc = 0;
  sscanf("200", "%hhd", &sc);
  CHECK(sc == (signed char)200);

  /* file-backed ungetc + remove + fscanf */
  FILE *f = fopen("TIER1.TMP", "w");
  if (f) {
    fputs("XYZ", f);
    fclose(f);
    f = fopen("TIER1.TMP", "r");
    if (f) {
      int c1 = fgetc(f); /* 'X' */
      CHECK(c1 == 'X');
      CHECK(ungetc(c1, f) == 'X');
      CHECK(fgetc(f) == 'X'); /* served the pushed-back byte */
      CHECK(fgetc(f) == 'Y');
      fclose(f);
    }
    CHECK(remove("TIER1.TMP") == 0);
  }
  f = fopen("TIER1.TMP", "w");
  if (f) {
    fputs("42\n", f);
    fclose(f);
    f = fopen("TIER1.TMP", "r");
    if (f) {
      int v = 0;
      CHECK(fscanf(f, "%d", &v) == 1 && v == 42);
      fclose(f);
    }
    remove("TIER1.TMP");
  }

  printf("TIER1 %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}

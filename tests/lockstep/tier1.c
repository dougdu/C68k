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

  /* scanf width, %n position, %i auto-base, and EOF-on-empty */
  int wv = 0, np = 0;
  CHECK(sscanf("12345", "%3d%n", &wv, &np) == 1 && wv == 123 && np == 3);
  CHECK(sscanf("  0x2A rest", "%i", &wv) == 1 && wv == 42);
  CHECK(sscanf("", "%d", &wv) == EOF);

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
  /* streaming fscanf: two ints share one line, a third is on the next line.
     The char-streaming scanner must leave the unparsed remainder in the
     stream so all three succeed across separate calls (the old line-buffered
     scanner dropped "20" with the rest of the first line). */
  f = fopen("TIER1.TMP", "w");
  if (f) {
    fputs("10 20\n30\n", f);
    fclose(f);
    f = fopen("TIER1.TMP", "r");
    if (f) {
      int p = 0, q = 0, r = 0;
      CHECK(fscanf(f, "%d", &p) == 1 && p == 10);
      CHECK(fscanf(f, "%d", &q) == 1 && q == 20); /* same line, kept */
      CHECK(fscanf(f, "%d", &r) == 1 && r == 30); /* next line */
      /* No 4th integer: clean EOF on both OSes -- text reads stop at the CP/M
         ^Z record padding just as they hit true EOF on Osiris. */
      CHECK(fscanf(f, "%d", &r) == EOF);
      fclose(f);
    }
    remove("TIER1.TMP");
  }

  /* rename: create, rename, confirm old gone / new holds the content */
  f = fopen("TIER1A.TMP", "w");
  if (f) {
    fputc('Z', f);
    fclose(f);
    CHECK(rename("TIER1A.TMP", "TIER1B.TMP") == 0);
    FILE *g = fopen("TIER1A.TMP", "r");
    CHECK(g == NULL); /* old name no longer resolves */
    if (g)
      fclose(g);
    g = fopen("TIER1B.TMP", "r");
    CHECK(g != NULL); /* new name holds the file */
    if (g) {
      CHECK(fgetc(g) == 'Z');
      fclose(g);
    }
    remove("TIER1B.TMP");
  }

  /* text vs binary mode: an embedded 0x1A (Ctrl-Z) ends a text read but is an
     ordinary byte for a binary read. */
  f = fopen("TIER1C.TMP", "wb");
  if (f) {
    static const unsigned char bin[3] = {'A', 0x1A, 'B'};
    fwrite(bin, 1, 3, f);
    fclose(f);
    f = fopen("TIER1C.TMP", "rb"); /* binary: reads straight through 0x1A */
    if (f) {
      unsigned char b[3] = {0, 0, 0};
      CHECK(fread(b, 1, 3, f) == 3);
      CHECK(b[0] == 'A' && b[1] == 0x1A && b[2] == 'B');
      fclose(f);
    }
    f = fopen("TIER1C.TMP", "r"); /* text: 0x1A is logical EOF */
    if (f) {
      CHECK(fgetc(f) == 'A');
      CHECK(fgetc(f) == EOF);
      CHECK(feof(f));
      fclose(f);
    }
    remove("TIER1C.TMP");
  }

  printf("TIER1 %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
